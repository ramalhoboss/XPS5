/* The title's screen, see video.h.
 *
 * The VideoOut set-up and the tile layout come from PS5_Vulkan's CPU
 * renderer (src/demo_renderer.cpp, Copyright (C) 2026 BlackBearReloaded and
 * Mihawk-99, GPL-3.0-or-later), so this file is under the same licence.
 *
 * The buffers are tiled and write-combined: reading them back is slow, and
 * so is writing them out of order. The picture is drawn linear in ordinary
 * memory and copied in 8x8 tiles, which are 256 contiguous bytes of the
 * buffer (the low 8 bits of a pixel's offset depend only on x & 7 and
 * y & 7, the rest only on the tile), so the copy writes them in order. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "video.h"
#include "workers.h"

#define MEMORY_ALIGNMENT  0x200000
#define FRAME_MIN         0x1000000 // what a 1080p frame always had
#define MEMORY_WC_GARLIC  3
#define MAP_PROTECTION    0x33
#define FORMAT_RGBA8_SRGB 0x8000000022000000ULL
#define FLIP_VSYNC        1


typedef struct video_buffer {
  void *data;
  void *metadata;
  void *reserved[2];
} video_buffer_t;

typedef struct video_attribute {
  uint8_t reserved[80];
} video_attribute_t;

size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t len, size_t align,
                                  int type, int64_t *phys);
int sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags, int64_t phys,
                             size_t align);
int sceSystemServiceHideSplashScreen(void);
int sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *param);
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
void sceVideoOutSetBufferAttribute2(video_attribute_t *attr, uint64_t format,
                                    uint32_t tiling, uint32_t width, uint32_t height,
                                    uint64_t option, uint32_t dcc_control,
                                    uint64_t dcc_clear_color);
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set, int32_t start,
                                video_buffer_t *buffers, int32_t count,
                                video_attribute_t *attr, int32_t category, void *option);
int sceVideoOutSubmitFlip(int32_t handle, int32_t index, uint32_t mode, int64_t arg);
int sceVideoOutWaitVblank(int32_t handle);
int sceVideoOutGetResolutionStatus(int32_t handle, void *status);


static int       g_video = -1;
static uint8_t  *g_frames[2];
static int       g_next;
static uint32_t *g_pixels;
static int64_t   g_flips;
static unsigned  g_width = 1920, g_height = 1080;
static unsigned  g_blocks_per_row; // of 128 x 128 pixels, 64 KiB each
static size_t    g_frame_used;     // the bytes of the blocks a frame has
static int       g_tv_w, g_tv_h, g_tv_ret = -1;
static const char *g_refused;      // the step that failed at the size asked, or NULL
static int       g_refused_h, g_refused_ret;


/**
 * byte offset of pixel (x, y) in a buffer, from demo_renderer.cpp
 **/
static size_t
tiled_offset(unsigned x, unsigned y) {
  uint32_t offset = ((y << 4) & 0x70) ^ ((y << 5) & 0xf00) ^ ((y << 9) & 0x1000) ^
                    ((y << 8) & 0x4000) ^ ((x << 2) & 0xc) ^ ((x << 5) & 0x380) ^
                    ((x << 4) & 0x400) ^ ((x << 6) & 0x800) ^ ((x << 9) & 0xa000);

  return ((size_t)((y >> 7) * g_blocks_per_row + (x >> 7)) << 16) + offset;
}


static void
flush(void *addr, size_t len) {
  uint8_t *at = addr, *end = at + len;

  for(; at < end; at += 64) {
    __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
  }
  __asm__ volatile("mfence" ::: "memory");
}


/**
 * copy the picture's rows from..to (multiples of 8) into a buffer; inside a
 * tile the offset of (x, y) is (y << 4) | (x & 3) << 2 | (x & 4) << 5: two
 * 4x8 halves of 4-pixel rows
 **/
static void
copy_tiled(uint8_t *frame, const uint32_t *src, unsigned from, unsigned to) {
  for(unsigned ty=from; ty < to && ty < g_height; ty += 8) {
    for(unsigned tx=0; tx < g_width; tx += 8) {
      uint32_t *dst = (uint32_t*)(frame + tiled_offset(tx, ty));
      const uint32_t *row = src + (size_t)ty * g_width + tx;

      for(unsigned half=0; half < 2; half++) {
        for(unsigned y=0; y < 8; y++) {
          memcpy(dst, row + (size_t)y * g_width + half * 4, 16);
          dst += 4;
        }
      }
    }
  }
}


/**
 * a part of the copy: whole rows of blocks, which are contiguous in the
 * buffer, so each part flushes its own bytes
 **/
static void
present_part(void *arg, int part, int parts) {
  const unsigned rows = (g_height + 127) >> 7;
  const size_t row_bytes = (size_t)g_blocks_per_row << 16;
  int from, to;

  (void)arg;
  workers_rows((int)rows, part, parts, &from, &to);
  if(from < to) {
    copy_tiled(g_frames[g_next], g_pixels, (unsigned)from << 7, (unsigned)to << 7);
    flush(g_frames[g_next] + from * row_bytes, (size_t)(to - from) * row_bytes);
  }
}


/**
 * what the TV gets, as VideoOut says: fullWidth and fullHeight lead its
 * resolution status
 **/
static void
read_tv(void) {
  uint32_t status[64]; // 48 bytes on the PS4, room for more

  memset(status, 0, sizeof status);
  if((g_tv_ret=sceVideoOutGetResolutionStatus(g_video, status)) >= 0) {
    g_tv_w = (int)status[0];
    g_tv_h = (int)status[1];
  }
}


/**
 * the picture's size for a height of 2160, 1440 or else 1080 rows
 **/
static void
set_size(int height) {
  g_height = height >= 2160 ? 2160 : height >= 1440 ? 1440 : 1080;
  g_width = g_height == 2160 ? 3840 : g_height == 1440 ? 2560 : 1920;
  g_blocks_per_row = (g_width + 127) >> 7;
  g_frame_used = (size_t)(((g_height + 127) >> 7) * g_blocks_per_row) << 16;
}


/**
 * step failed with ret at the picture's size: noted when it is above 1080p,
 * which then tries again at 1080p; returns whether it does
 **/
static int
refused(const char *step, int ret) {
  if(g_height <= 1080) {
    return 0;
  }
  g_refused = step;
  g_refused_h = (int)g_height;
  g_refused_ret = ret;
  return 1;
}


int
video_open(int height, const char **step) {
  video_buffer_t buffers[2];
  video_attribute_t attr;
  int64_t phys = 0;
  uint8_t *mapped = NULL;
  size_t pool, frame_bytes;
  int ret;

  g_refused = NULL;
  sceSystemServiceHideSplashScreen();
  *step = "sceVideoOutOpen";
  if((g_video=sceVideoOutOpen(0xff, 0, 0, NULL)) < 0) {
    return g_video;
  }
  read_tv();
  if(!height) {
    // as the TV: VideoOut takes the TV's own size (a TV of 2160p alone
    // refuses 1440p buffers, 0x80290005) or 1080p, which it scales up
    height = g_tv_h >= 2160 ? 2160 : g_tv_h >= 1440 ? 1440 : 1080;
  }
  // a step that fails above 1080p tries again at 1080p, in the memory it
  // already has
  for(set_size(height); ; set_size(1080)) {
    frame_bytes = (g_frame_used + MEMORY_ALIGNMENT - 1) & ~(size_t)(MEMORY_ALIGNMENT - 1);
    frame_bytes = frame_bytes < FRAME_MIN ? FRAME_MIN : frame_bytes;

    *step = "picture memory";
    mem_free(g_pixels);
    if(!(g_pixels=mem_calloc((size_t)g_width * g_height, sizeof *g_pixels))) {
      if(refused(*step, -1)) {
        continue;
      }
      return -1;
    }
    if(!mapped) {
      *step = "direct memory";
      ret = -1;
      if((pool=sceKernelGetDirectMemorySize()) >= frame_bytes * 2 &&
         (ret=sceKernelAllocateDirectMemory(0, (int64_t)pool, frame_bytes * 2, MEMORY_ALIGNMENT,
                                            MEMORY_WC_GARLIC, &phys)) >= 0) {
        *step = "sceKernelMapDirectMemory";
        void *at = NULL;
        if((ret=sceKernelMapDirectMemory(&at, frame_bytes * 2, MAP_PROTECTION, 0, phys,
                                         MEMORY_ALIGNMENT)) >= 0) {
          mapped = at;
          memset(mapped, 0, frame_bytes * 2);
          flush(mapped, frame_bytes * 2);
        }
      }
      if(!mapped) {
        if(refused(*step, ret)) {
          continue;
        }
        return ret;
      }
    }
    g_frames[0] = mapped;
    g_frames[1] = mapped + frame_bytes;

    memset(buffers, 0, sizeof buffers);
    buffers[0].data = g_frames[0];
    buffers[1].data = g_frames[1];
    memset(&attr, 0, sizeof attr);
    sceVideoOutSetFlipRate(g_video, 0);
    sceVideoOutSetBufferAttribute2(&attr, FORMAT_RGBA8_SRGB, 0, g_width, g_height, 0, 0, 0);
    *step = "sceVideoOutRegisterBuffers2";
    if((ret=sceVideoOutRegisterBuffers2(g_video, 0, 0, buffers, 2, &attr, 0, NULL)) >= 0) {
      return 0;
    }
    if(!refused(*step, ret)) {
      return ret;
    }
  }
}


int
video_refused(int *height, int *code, const char **step) {
  if(!g_refused) {
    return 0;
  }
  *height = g_refused_h;
  *code = g_refused_ret;
  *step = g_refused;
  return 1;
}


uint32_t *
video_pixels(void) {
  return g_pixels;
}


int
video_width(void) {
  return (int)g_width;
}


int
video_height(void) {
  return (int)g_height;
}


int
video_tv(int *w, int *h) {
  *w = g_tv_w;
  *h = g_tv_h;
  return g_tv_ret;
}


void
video_present(void) {
  workers_run(present_part, NULL);
  sceVideoOutSubmitFlip(g_video, g_next, FLIP_VSYNC, ++g_flips);
  sceVideoOutWaitVblank(g_video);
  g_next ^= 1;
}


void
video_wait(void) {
  sceVideoOutWaitVblank(g_video);
}
