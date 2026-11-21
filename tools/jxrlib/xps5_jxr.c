/* XPS5's door to jxrlib (Microsoft's JPEG XR library, BSD 2-Clause): a
JPEG XR file in memory, read for its size and pixel format, and decoded to
the pixels it keeps. The PS5 saves a screenshot of a game in HDR as .jxr
(2026-11-14). tools/jxrlib/build.sh builds this with jxrlib into one
archive; build-title.sh sends its malloc, calloc, realloc and free to the
title's memory (title/ffmem.c). title/jxr.c turns the pixels into a
picture X takes. */

#include <stdlib.h>
#include <string.h>

#include "JXRGlue.h"

int xps5_jxr_info(const void *data, size_t len, int *w, int *h, int *format, int *bits);
int xps5_jxr_decode(const void *data, size_t len, int shrink, void *out, size_t room,
                    int *w, int *h, int *stride);


#ifdef XPS5_JXR_NO_TMPNAM
/*
 * The PS5's libc has no tmpnam, which only jxrlib's encoder asks for (never
 * called here); build.sh renames its calls to this.
 */
char *xps5_jxr_tmpnam(char *s);

char *
xps5_jxr_tmpnam(char *s) {
  (void)s;
  return NULL;
}
#endif


/**
 * a decoder of the file, its head read
 **/
static ERR
open_decoder(const void *data, size_t len, PKImageDecode **dec, PKPixelInfo *pi) {
  struct WMPStream *ws = NULL;
  ERR err;

  *dec = NULL;
  if(!data || !len) {
    return WMP_errInvalidParameter;
  }
  if((err=CreateWS_Memory(&ws, (void *)data, len)) < 0) {
    return err;
  }
  if((err=PKImageDecode_Create_WMP(dec)) < 0) {
    ws->Close(&ws);
    return err;
  }
  if((err=(*dec)->Initialize(*dec, ws)) < 0) {
    (*dec)->Release(dec);
    ws->Close(&ws);
    return err;
  }
  (*dec)->fStreamOwner = !0;
  memset(pi, 0, sizeof *pi);
  pi->pGUIDPixFmt = &(*dec)->guidPixFormat;
  if((err=PixelFormatLookup(pi, LOOKUP_FORWARD)) < 0) {
    (*dec)->Release(dec);
    return err;
  }
  return WMP_errSuccess;
}


/**
 * the size, the pixel format (the last byte of its GUID, 0x100 more for one
 * outside the 6fddc324 family) and the bits of a pixel; 0, or jxrlib's
 * error
 **/
int
xps5_jxr_info(const void *data, size_t len, int *w, int *h, int *format, int *bits) {
  PKImageDecode *dec;
  PKPixelInfo pi;
  ERR err;

  if((err=open_decoder(data, len, &dec, &pi)) < 0) {
    return err;
  }
  *w = (int)dec->WMP.wmiI.cWidth;
  *h = (int)dec->WMP.wmiI.cHeight;
  *format = (dec->guidPixFormat.Data1 == 0x6fddc324 ? 0 : 0x100) | dec->guidPixFormat.Data4[7];
  *bits = (int)pi.cbitUnit;
  dec->Release(&dec);
  return 0;
}


/**
 * the pixels, as the file keeps them, at 1/2^shrink of its size (0 to 4:
 * JPEG XR keeps the smaller sizes in its bands, which decode faster), into
 * out (room bytes); their size and the bytes of a row in w, h and stride.
 * 0, or jxrlib's error
 **/
int
xps5_jxr_decode(const void *data, size_t len, int shrink, void *out, size_t room,
                int *w, int *h, int *stride) {
  PKImageDecode *dec;
  PKPixelInfo pi;
  PKRect rect;
  size_t side;
  ERR err;

  if(shrink < 0 || shrink > 4) {
    return WMP_errInvalidParameter;
  }
  if((err=open_decoder(data, len, &dec, &pi)) < 0) {
    return err;
  }
  dec->WMP.wmiSCP.uAlphaMode = (pi.grBit & PK_pixfmtHasAlpha) ? 2 : 0;
  dec->WMP.wmiSCP.sbSubband = SB_ALL;
  dec->WMP.bIgnoreOverlap = FALSE;
  dec->WMP.wmiI.cfColorFormat = pi.cfColorFormat;
  dec->WMP.wmiI.bdBitDepth = pi.bdBitDepth;
  dec->WMP.wmiI.cBitsPerUnit = pi.cbitUnit;
  side = (size_t)1 << shrink;
  dec->WMP.wmiI.cThumbnailWidth = (dec->WMP.wmiI.cWidth + side - 1) / side;
  dec->WMP.wmiI.cThumbnailHeight = (dec->WMP.wmiI.cHeight + side - 1) / side;
  dec->WMP.wmiI.bSkipFlexbits = FALSE;
  if(shrink && (dec->WMP.wmiI.cfColorFormat == YUV_420 || dec->WMP.wmiI.cfColorFormat == YUV_422)) {
    dec->WMP.wmiI.cfColorFormat = YUV_444;
  }
  dec->WMP.wmiI.cROILeftX = 0;
  dec->WMP.wmiI.cROITopY = 0;
  dec->WMP.wmiI.cROIWidth = dec->WMP.wmiI.cThumbnailWidth;
  dec->WMP.wmiI.cROIHeight = dec->WMP.wmiI.cThumbnailHeight;
  dec->WMP.wmiI.oOrientation = O_NONE;
  dec->WMP.wmiI.cPostProcStrength = 0;
  dec->WMP.wmiSCP.bVerbose = FALSE;

  *w = (int)dec->WMP.wmiI.cROIWidth;
  *h = (int)dec->WMP.wmiI.cROIHeight;
  *stride = (int)(((pi.cbitUnit + 7) >> 3) * (size_t)*w);
  if(*w <= 0 || *h <= 0 || (size_t)*stride * (size_t)*h > room) {
    dec->Release(&dec);
    return WMP_errOutOfMemory;
  }
  rect.X = 0;
  rect.Y = 0;
  rect.Width = *w;
  rect.Height = *h;
  err = dec->Copy(dec, &rect, (U8 *)out, (U32)*stride);
  dec->Release(&dec);
  return err < 0 ? err : 0;
}
