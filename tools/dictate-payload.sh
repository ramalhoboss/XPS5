#!/bin/sh
# XPS5's dictation payload (src/dictated.c) with whisper.cpp, built with the
# payload SDK's own compilers: C++ with its exceptions, RTTI and threads,
# which the payload SDK links (libc++, libc++abi, libunwind) and the title
# does not. build.ps1 -Wsl runs it; whisper.cpp is built again only when its
# sources or tools/build-whisper.sh change.
#
# Use: sh tools/dictate-payload.sh <out dir>   (writes <out dir>/XPS5-dictate.elf)
#      PS5_PAYLOAD_SDK=<sdk> picks the SDK (PS5_Vulkan's by default)
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
sdk=${PS5_PAYLOAD_SDK:-$HOME/src/PS5_Vulkan/.deps/native/ps5-payload-sdk}
out=${1:?"use: dictate-payload.sh <out dir>"}
work=$out/dictate
cc="$sdk/bin/prospero-clang"
cxx="$sdk/bin/prospero-clang++"
[ -x "$cc" ] && [ -x "$cxx" ] || { echo "no payload SDK at $sdk" >&2; exit 1; }
export PATH=/usr/lib/llvm-18/bin:$PATH
mkdir -p "$work"

lib=$work/libxps5whisper.a
if [ ! -f "$lib" ] || [ -n "$(find "$root/third_party/whisper.cpp" "$root/tools/build-whisper.sh" -newer "$lib" -type f | head -1)" ]; then
  rm -f "$lib"
  sh "$root/tools/build-whisper.sh" "$cc" -O2 -fPIC -- "$cxx" -O2 -fPIC -- "$sdk/bin/llvm-ar" "$lib"
fi

w="-I $root/third_party/whisper.cpp/include -I $root/third_party/whisper.cpp/ggml/include"
flags="-std=c11 -O2 -Wall -Werror -I $root/src"
"$cc" $flags $w -c "$root/src/dictate.c" -o "$work/dictate.o"
"$cc" $flags -c "$root/src/dictserve.c" -o "$work/dictserve.o"
"$cc" $flags -c "$root/src/dictated.c" -o "$work/dictated.o"
"$cxx" -o "$out/XPS5-dictate.elf" "$work/dictated.o" "$work/dictserve.o" "$work/dictate.o" "$lib" -lpthread
"$sdk/bin/llvm-strip" --strip-debug "$out/XPS5-dictate.elf"
echo "dictate: $out/XPS5-dictate.elf ($(wc -c < "$out/XPS5-dictate.elf") bytes)"
