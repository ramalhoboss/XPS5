/* Ultra HDR's file, see ultrahdr.h. The layout, as the Ultra HDR image
format 1.0 gives it:

  SOI, APP0 (JFIF), APP1 (XMP: hdrgm:Version, the container's two items),
  APP2 (MPF: two pictures, the second's offset), the SDR picture's segments,
  EOI; then SOI, APP0, APP1 (XMP: the gain map's hdrgm values), the gain
  map's segments, EOI.

MPF's offsets count from its TIFF header (its "MM"), big-endian. */

#include <stdio.h>
#include <string.h>

#include "mem.h"
#include "ultrahdr.h"

#define XMP_NS     "http://ns.adobe.com/xap/1.0/"
#define XMP_NS_LEN 29 // with its 0
#define MPF_SIZE   90 // the APP2 segment, marker and length included
#define MPF_TIFF   8  // where the TIFF header starts in it
#define GAIN_OFFSET "0.015625" // 1/64, title/jxr.c's

static const char g_primary_xmp[] =
  "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"XMP Core 5.5.0\">"
  "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
  "<rdf:Description rdf:about=\"\""
  " xmlns:Container=\"http://ns.google.com/photos/1.0/container/\""
  " xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\""
  " xmlns:hdrgm=\"http://ns.adobe.com/hdr-gain-map/1.0/\""
  " hdrgm:Version=\"1.0\">"
  "<Container:Directory><rdf:Seq>"
  "<rdf:li rdf:parseType=\"Resource\">"
  "<Container:Item Item:Semantic=\"Primary\" Item:Mime=\"image/jpeg\"/>"
  "</rdf:li>"
  "<rdf:li rdf:parseType=\"Resource\">"
  "<Container:Item Item:Semantic=\"GainMap\" Item:Mime=\"image/jpeg\" Item:Length=\"%zu\"/>"
  "</rdf:li>"
  "</rdf:Seq></Container:Directory>"
  "</rdf:Description></rdf:RDF></x:xmpmeta>";

static const char g_gain_xmp[] =
  "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"XMP Core 5.5.0\">"
  "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
  "<rdf:Description rdf:about=\"\""
  " xmlns:hdrgm=\"http://ns.adobe.com/hdr-gain-map/1.0/\""
  " hdrgm:Version=\"1.0\""
  " hdrgm:GainMapMin=\"0\""
  " hdrgm:GainMapMax=\"%.6f\""
  " hdrgm:Gamma=\"1\""
  " hdrgm:OffsetSDR=\"" GAIN_OFFSET "\""
  " hdrgm:OffsetHDR=\"" GAIN_OFFSET "\""
  " hdrgm:HDRCapacityMin=\"0\""
  " hdrgm:HDRCapacityMax=\"%.6f\""
  " hdrgm:BaseRenditionIsHDR=\"False\"/>"
  "</rdf:RDF></x:xmpmeta>";


/**
 * where a JPEG's segments go after its SOI and its JFIF APP0, if any; 0
 * when it is not a JPEG
 **/
static size_t
after_head(const uint8_t *j, size_t len) {
  size_t at = 2;

  if(len < 4 || j[0] != 0xff || j[1] != 0xd8) {
    return 0;
  }
  if(len >= 6 && j[2] == 0xff && j[3] == 0xe0) {
    at += 2 + ((size_t)j[4] << 8 | j[5]);
  }
  return at < len ? at : 0;
}


static uint8_t *
put16(uint8_t *p, unsigned v) {
  *p++ = (uint8_t)(v >> 8);
  *p++ = (uint8_t)v;
  return p;
}


static uint8_t *
put32(uint8_t *p, uint32_t v) {
  p = put16(p, v >> 16);
  return put16(p, v & 0xffff);
}


static uint8_t *
put_xmp(uint8_t *p, const char *xml, size_t n) {
  p = put16(p, 0xffe1);
  p = put16(p, (unsigned)(2 + XMP_NS_LEN + n));
  memcpy(p, XMP_NS, XMP_NS_LEN); // its 0 too
  memcpy(p + XMP_NS_LEN, xml, n);
  return p + XMP_NS_LEN + n;
}


/**
 * MPF's APP2: two pictures, the primary (the whole first JPEG) and the gain
 * map at offset from the TIFF header
 **/
static uint8_t *
put_mpf(uint8_t *p, uint32_t primary, uint32_t gain, uint32_t offset) {
  p = put16(p, 0xffe2);
  p = put16(p, MPF_SIZE - 2);
  memcpy(p, "MPF", 4);
  p += 4;
  // TIFF header, big-endian, its first IFD right after it
  memcpy(p, "MM\0\x2a", 4);
  p = put32(p + 4, 8);
  p = put16(p, 3);
  p = put16(p, 0xb000); // MPFVersion
  p = put16(p, 7);
  p = put32(p, 4);
  memcpy(p, "0100", 4);
  p += 4;
  p = put16(p, 0xb001); // NumberOfImages
  p = put16(p, 4);
  p = put32(p, 1);
  p = put32(p, 2);
  p = put16(p, 0xb002); // MPEntry, after this IFD
  p = put16(p, 7);
  p = put32(p, 32);
  p = put32(p, 8 + 2 + 3 * 12 + 4);
  p = put32(p, 0);      // no next IFD
  p = put32(p, 0x00030000); // the primary picture, a baseline JPEG
  p = put32(p, primary);
  p = put32(p, 0);
  p = put32(p, 0);
  p = put32(p, 0);      // the gain map
  p = put32(p, gain);
  p = put32(p, offset);
  return put32(p, 0);
}


int
ultrahdr_join(const uint8_t *sdr, size_t sdr_len, const uint8_t *gain, size_t gain_len, float gain_max,
              uint8_t **out, size_t *len) {
  char gain_xml[1024], primary_xml[1536];
  size_t sdr_head, gain_head, gain_total, primary_total, tiff;
  int n, m;
  uint8_t *p;

  *out = NULL;
  *len = 0;
  if(!(sdr_head=after_head(sdr, sdr_len)) || !(gain_head=after_head(gain, gain_len)) || !(gain_max > 0)) {
    return -1;
  }
  n = snprintf(gain_xml, sizeof gain_xml, g_gain_xmp, gain_max, gain_max);
  if(n <= 0 || n >= (int)sizeof gain_xml) {
    return -1;
  }
  gain_total = gain_len + 4 + XMP_NS_LEN + (size_t)n;
  m = snprintf(primary_xml, sizeof primary_xml, g_primary_xmp, gain_total);
  if(m <= 0 || m >= (int)sizeof primary_xml) {
    return -1;
  }
  primary_total = sdr_len + 4 + XMP_NS_LEN + (size_t)m + MPF_SIZE;
  tiff = sdr_head + 4 + XMP_NS_LEN + (size_t)m + MPF_TIFF;
  if(primary_total + gain_total > 0xffffffffu || !(p=mem_alloc(primary_total + gain_total))) {
    return -1;
  }
  *out = p;
  *len = primary_total + gain_total;

  memcpy(p, sdr, sdr_head);
  p = put_xmp(p + sdr_head, primary_xml, (size_t)m);
  p = put_mpf(p, (uint32_t)primary_total, (uint32_t)gain_total, (uint32_t)(primary_total - tiff));
  memcpy(p, sdr + sdr_head, sdr_len - sdr_head);
  p += sdr_len - sdr_head;
  memcpy(p, gain, gain_head);
  p = put_xmp(p + gain_head, gain_xml, (size_t)n);
  memcpy(p, gain + gain_head, gain_len - gain_head);
  return 0;
}


static int
has(const uint8_t *s, size_t len, const char *what) {
  size_t n = strlen(what);

  for(size_t i=0; n && i + n <= len; i++) {
    if(s[i] == (uint8_t)what[0] && !memcmp(s + i, what, n)) {
      return 1;
    }
  }
  return 0;
}


void
ultrahdr_inspect(const uint8_t *jpeg, size_t len, ultrahdr_info_t *info) {
  size_t at = 2, seg;

  memset(info, 0, sizeof *info);
  if(!jpeg || len < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8) {
    return;
  }
  for(size_t i=0; i + 2 < len; i++) {
    info->pictures += jpeg[i] == 0xff && jpeg[i + 1] == 0xd8 && jpeg[i + 2] == 0xff;
  }
  info->gain = has(jpeg, len, "hdrgm:GainMapMax");
  // the first picture's segments, up to its scan
  while(at + 4 <= len && jpeg[at] == 0xff && jpeg[at + 1] != 0xda && jpeg[at + 1] != 0xd9) {
    seg = (size_t)jpeg[at + 2] << 8 | jpeg[at + 3];
    if(seg < 2 || at + 2 + seg > len) {
      break;
    }
    if(jpeg[at + 1] == 0xe1 && seg > 2 + XMP_NS_LEN && !memcmp(jpeg + at + 4, XMP_NS, XMP_NS_LEN)) {
      info->xmp |= has(jpeg + at + 4, seg - 2, "hdrgm:Version");
    } else if(jpeg[at + 1] == 0xe2 && seg >= 6 && !memcmp(jpeg + at + 4, "MPF", 4)) {
      info->mpf = 1;
    } else if(jpeg[at + 1] == 0xe2 && seg >= 2 + 28 && !memcmp(jpeg + at + 4, "urn:iso:std:iso:ts:21496:-1", 28)) {
      info->iso = 1;
    }
    at += 2 + seg;
  }
}
