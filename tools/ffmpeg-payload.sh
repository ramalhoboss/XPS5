#!/bin/sh
# Builds the FFmpeg the notifications payload (src/notify.c) plays a Space
# with: libavcodec with only its AAC decoder, libswresample and libavutil,
# for the ps5-payload-sdk. The title's FFmpeg has every decoder of the
# videos; linked whole into the payload it would make it eight times its
# size, as libavcodec's context lists all the codecs it has. FFmpeg is under
# the LGPL 2.1 or later. Built once, kept in CACHE, copied to <out dir>
# (include/ and lib/), which build.ps1 compiles the payload against.
#
# Usage: sh tools/ffmpeg-payload.sh <out dir>
#   FFMPEG_TAR       FFmpeg's source (default ~/browserps5-build/deps/sources/ffmpeg-8.1.3.tar.xz)
#   PS5_PAYLOAD_SDK  the SDK (default /opt/ps5-payload-sdk)
#   NASM             FFmpeg's assembler (default ~/browserps5-build/ext/tools/bin/nasm; without
#                    it the C versions are built)
#   CACHE            where it is built and kept (default ~/xps5-build/ffmpeg-payload)

set -eu

out=${1:?usage: ffmpeg-payload.sh <out dir>}
tar=${FFMPEG_TAR:-$HOME/browserps5-build/deps/sources/ffmpeg-8.1.3.tar.xz}
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
nasm=${NASM:-$HOME/browserps5-build/ext/tools/bin/nasm}
cache=${CACHE:-$HOME/xps5-build/ffmpeg-payload}

if [ ! -f "$cache/lib/libavcodec.a" ]; then
    [ -f "$tar" ] || { echo "no FFmpeg source in $tar (set FFMPEG_TAR)" >&2; exit 2; }
    [ -x "$sdk/bin/prospero-clang" ] || { echo "no ps5-payload-sdk in $sdk (set PS5_PAYLOAD_SDK)" >&2; exit 2; }
    if [ -x "$nasm" ]; then
        asm="--x86asmexe=$nasm"
    else
        asm=--disable-x86asm
    fi
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    tar -xf "$tar" -C "$work"
    src=$(find "$work" -mindepth 1 -maxdepth 1 -type d -name 'ffmpeg-*' | head -n 1)
    [ -n "$src" ] || { echo "no ffmpeg-* folder in $tar" >&2; exit 2; }
    (
        cd "$src"
        ./configure --prefix=/ --enable-cross-compile --arch=x86_64 --target-os=freebsd \
            --cc="$sdk/bin/prospero-clang" --ar="$sdk/bin/prospero-ar" --ranlib="$sdk/bin/prospero-ranlib" \
            --nm="$sdk/bin/prospero-nm" --strip="$sdk/bin/prospero-strip" "$asm" \
            --enable-static --disable-shared --enable-pic --disable-debug --disable-doc --disable-programs \
            --disable-autodetect --disable-network --disable-pthreads --disable-everything \
            --disable-avformat --disable-avfilter --disable-swscale --disable-avdevice \
            --enable-avcodec --enable-avutil --enable-swresample --enable-decoder=aac > "$work/configure.log" \
            || { tail -n 20 "$work/configure.log" >&2; exit 1; }
        make -j"$(nproc)" > "$work/make.log" 2>&1 || { tail -n 30 "$work/make.log" >&2; exit 1; }
        rm -rf "$cache"
        make install DESTDIR="$work/stage" > /dev/null
    )
    mkdir -p "$cache"
    cp -r "$work/stage/include" "$cache/include"
    mkdir -p "$cache/lib"
    cp "$work/stage/lib/libavcodec.a" "$work/stage/lib/libswresample.a" "$work/stage/lib/libavutil.a" "$cache/lib/"
    echo "ffmpeg for the payload: built in $cache"
fi

rm -rf "$out"
mkdir -p "$out"
cp -r "$cache/include" "$cache/lib" "$out/"
