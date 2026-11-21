#!/bin/sh
# Builds jxrlib's decoder with tools/jxrlib/xps5_jxr.c into one archive,
# libxps5jxr.a: for the PS5 with PS5_Vulkan's compiler wrapper, or for the
# host (the tests). jxrlib is Microsoft's JPEG XR library under the BSD
# 2-Clause licence (its LICENSE goes along in the out folder); XPS5 uses it
# for the PS5's HDR screenshots, which are .jxr.
#
# Usage: sh tools/jxrlib/build.sh <jxrlib source> <out dir> ps5|host
#   PS5VK   a PS5_Vulkan checkout with its dependencies (default ~/src/PS5_Vulkan)

set -eu

src=${1:?usage: build.sh <jxrlib source> <out dir> ps5|host}
out=${2:?usage: build.sh <jxrlib source> <out dir> ps5|host}
target=${3:?usage: build.sh <jxrlib source> <out dir> ps5|host}
here=$(cd -- "$(dirname -- "$0")" && pwd)
ps5vk=${PS5VK:-$HOME/src/PS5_Vulkan}

[ -f "$src/jxrgluelib/JXRGlue.c" ] || { echo "no jxrlib in $src" >&2; exit 2; }
case $target in
  ps5)
    sdk=$ps5vk/.deps/native/ps5-payload-sdk
    [ -x "$sdk/bin/prospero-lld" ] || { echo "run tools/setup-native-dependencies.sh in $ps5vk first" >&2; exit 2; }
    cc() { PATH=/usr/lib/llvm-18/bin:$PATH PS5_PAYLOAD_SDK=$sdk sh "$ps5vk/tooling/prospero-clang18" "$@"; }
    ar=/usr/lib/llvm-18/bin/llvm-ar
    # the PS5's libc has neither __assert nor tmpnam (see xps5_jxr.c)
    extra="-DNDEBUG -Dtmpnam=xps5_jxr_tmpnam -DXPS5_JXR_NO_TMPNAM"
    ;;
  host)
    cc() { command cc "$@"; }
    ar=ar
    extra=
    ;;
  *)
    echo "the target is ps5 or host" >&2
    exit 2
    ;;
esac

rm -rf "$out"
mkdir -p "$out/obj"
flags="-std=gnu11 -O2 -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion
  -ffunction-sections -fdata-sections -D__ANSI__ -DDISABLE_PERF_MEASUREMENT $extra
  -I$src -I$src/common/include -I$src/image/sys -I$src/jxrgluelib"
objects=
for file in image/sys/adapthuff.c image/sys/image.c image/sys/strcodec.c image/sys/strPredQuant.c \
    image/sys/strTransform.c image/sys/perfTimerANSI.c \
    image/decode/decode.c image/decode/postprocess.c image/decode/segdec.c image/decode/strdec.c \
    image/decode/strInvTransform.c image/decode/strPredQuantDec.c image/decode/JXRTranscode.c \
    image/encode/encode.c image/encode/segenc.c image/encode/strenc.c image/encode/strFwdTransform.c \
    image/encode/strPredQuantEnc.c \
    jxrgluelib/JXRGlue.c jxrgluelib/JXRMeta.c jxrgluelib/JXRGluePFC.c jxrgluelib/JXRGlueJxr.c; do
  object=$out/obj/$(echo "$file" | tr / _).o
  # shellcheck disable=SC2086
  cc $flags -c "$src/$file" -o "$object"
  objects="$objects $object"
done
# ours with jxrlib's headers, warnings on
# shellcheck disable=SC2086
cc -std=gnu11 -O2 -Wall -ffunction-sections -fdata-sections -D__ANSI__ -DDISABLE_PERF_MEASUREMENT $extra \
  -I"$src" -I"$src/common/include" -I"$src/image/sys" -I"$src/jxrgluelib" \
  -c "$here/xps5_jxr.c" -o "$out/obj/xps5_jxr.o"
# shellcheck disable=SC2086
"$ar" rcs "$out/libxps5jxr.a" $objects "$out/obj/xps5_jxr.o"
cp "$src/LICENSE" "$out/LICENSE-jxrlib"
rm -rf "$out/obj"
echo "jxrlib ($target): $out/libxps5jxr.a"
