#!/usr/bin/env python3
"""The AAC tables src/voice.c takes from libavcodec, for a host build
without it: read from FFmpeg's own libavcodec/aactab.c (LGPL 2.1 or later)
into a C file of the same names. Only the host test uses it; the title links
libavcodec, which has them.

    python3 tools/aac_tables.py <ffmpeg source>/libavcodec/aactab.c out.c
"""

import re
import sys


def array(src, name):
    m = re.search(r"\b" + re.escape(name) + r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("aac_tables: no %s in %s" % (name, sys.argv[1]))
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    values = [v.strip() for v in body.split(",") if v.strip()]
    for v in values:
        if not re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)[uU]?", v):
            sys.exit("aac_tables: %s has %r" % (name, v))
    return values


def main():
    src = open(sys.argv[1], encoding="utf-8").read()
    codes = array(src, "codes11")
    lens = array(src, "bits11")
    sf_code = array(src, "ff_aac_scalefactor_code")
    sf_bits = array(src, "ff_aac_scalefactor_bits")
    swb48 = array(src, "swb_offset_1024_48")
    swbs = array(src, "ff_aac_num_swb_1024")
    if len(codes) != 289 or len(lens) != 289 or len(sf_code) != 121 or len(sf_bits) != 121 or len(swbs) != 13:
        sys.exit("aac_tables: tables of other sizes than AAC's")
    out = ["/* from FFmpeg's libavcodec/aactab.c (LGPL 2.1 or later), by tools/aac_tables.py */",
           "#include <stddef.h>", "#include <stdint.h>",
           "static const uint16_t codes11[289] = {%s};" % ", ".join(codes),
           "static const uint8_t bits11[289] = {%s};" % ", ".join(lens),
           "static const uint16_t swb_offset_1024_48[] = {%s};" % ", ".join(swb48),
           "const uint16_t *const ff_aac_spectral_codes[11] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "
           "NULL, NULL, codes11};",
           "const uint8_t *const ff_aac_spectral_bits[11] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "
           "NULL, NULL, bits11};",
           "const uint32_t ff_aac_scalefactor_code[121] = {%s};" % ", ".join(sf_code),
           "const uint8_t ff_aac_scalefactor_bits[121] = {%s};" % ", ".join(sf_bits),
           "const uint16_t *const ff_swb_offset_1024[13] = {NULL, NULL, NULL, swb_offset_1024_48, "
           "swb_offset_1024_48, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};",
           "const uint8_t ff_aac_num_swb_1024[] = {%s};" % ", ".join(swbs), ""]
    open(sys.argv[2], "w", encoding="utf-8").write("\n".join(out))


main()
