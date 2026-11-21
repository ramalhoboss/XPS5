#!/bin/sh
# The dictation model in 8 bits: models/whisper/ggml-tiny-q8_0.bin, the one
# the installer carries, made from models/whisper/ggml-tiny.bin (16 bits) by
# tools/whisper-quantize.c on this machine, without the internet. The same
# input always gives the same bytes, so the installer's stamp only changes
# when the model does.
#   whisper-q8.sh <work dir>
# The work dir keeps the host's whisper archive (tools/build-whisper.sh with
# clang-18) for the next time.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
work=${1:?use: whisper-q8.sh <work dir>}
mkdir -p "$work"
w="$here/third_party/whisper.cpp"
[ -f "$work/libxps5whisper.a" ] || sh "$here/tools/build-whisper.sh" clang-18 -O2 -- clang++-18 -O2 -- ar "$work/libxps5whisper.a"
clang-18 -std=c11 -O2 -Wall -Werror -I "$w/ggml/include" -c "$here/tools/whisper-quantize.c" -o "$work/whisper-quantize.o"
clang++-18 -o "$work/whisper-quantize" "$work/whisper-quantize.o" "$work/libxps5whisper.a" -lpthread -lm
"$work/whisper-quantize" "$here/models/whisper/ggml-tiny.bin" "$here/models/whisper/ggml-tiny-q8_0.bin.part"
mv "$here/models/whisper/ggml-tiny-q8_0.bin.part" "$here/models/whisper/ggml-tiny-q8_0.bin"
ls -l "$here/models/whisper/ggml-tiny.bin" "$here/models/whisper/ggml-tiny-q8_0.bin"
