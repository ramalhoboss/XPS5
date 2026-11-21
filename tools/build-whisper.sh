#!/bin/sh
# whisper.cpp v1.9.5 and the ggml it carries (third_party/whisper.cpp), for
# dictation without the internet, in one archive:
#   build-whisper.sh <C compiler and its flags...> -- <C++ compiler and its flags...> -- <archiver> <out.a>
# For example: build-whisper.sh clang-18 -O2 -- clang++-18 -O2 -- ar build/libxps5whisper.a
# Without CMake: the definitions its build would choose for an x86-64 CPU
# with AVX2, FMA and F16C (the PS5's Zen 2 has them), the CPU backend linked
# in (no backends loaded from files), and llamafile's matrix code.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
cc=
while [ "$1" != "--" ]; do
    cc="$cc $1"
    shift
done
shift
cxx=
while [ "$1" != "--" ]; do
    cxx="$cxx $1"
    shift
done
shift
ar=$1
out=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

w="$here/third_party/whisper.cpp"
arch="-mavx -mavx2 -mfma -mf16c -mbmi2 -DGGML_AVX -DGGML_AVX2 -DGGML_FMA -DGGML_F16C -DGGML_BMI2"
defs="-DGGML_USE_CPU -DGGML_USE_LLAMAFILE -DGGML_SCHED_MAX_COPIES=4 -D_GNU_SOURCE -DNDEBUG"
defs="$defs -DWHISPER_VERSION=\"1.9.5\""
incs="-I $w/include -I $w/src -I $w/ggml/include -I $w/ggml/src -I $w/ggml/src/ggml-cpu"
n=0
for source in ggml/src/ggml.c ggml/src/ggml-alloc.c ggml/src/ggml-quants.c \
              ggml/src/ggml-cpu/ggml-cpu.c ggml/src/ggml-cpu/quants.c ggml/src/ggml-cpu/arch/x86/quants.c; do
    n=$((n + 1))
    $cc -std=c11 -w $arch $defs $incs -c "$w/$source" -o "$work/w$n.o"
done
for source in ggml/src/ggml.cpp ggml/src/ggml-backend.cpp ggml/src/ggml-backend-reg.cpp ggml/src/ggml-backend-dl.cpp \
              ggml/src/ggml-backend-meta.cpp ggml/src/ggml-threading.cpp ggml/src/ggml-opt.cpp ggml/src/gguf.cpp \
              ggml/src/ggml-cpu/ggml-cpu.cpp ggml/src/ggml-cpu/ops.cpp ggml/src/ggml-cpu/vec.cpp \
              ggml/src/ggml-cpu/binary-ops.cpp ggml/src/ggml-cpu/unary-ops.cpp ggml/src/ggml-cpu/repack.cpp \
              ggml/src/ggml-cpu/traits.cpp ggml/src/ggml-cpu/hbm.cpp ggml/src/ggml-cpu/arch/x86/repack.cpp \
              ggml/src/ggml-cpu/llamafile/sgemm.cpp ggml/src/ggml-cpu/amx/amx.cpp ggml/src/ggml-cpu/amx/mmq.cpp \
              ggml/src/ggml-cpu/tiled/tiled.cpp ggml/src/ggml-cpu/tiled/tiled-kernel.cpp \
              src/whisper.cpp src/aneforge/whisper-aneforge.cpp; do
    n=$((n + 1))
    $cxx -std=c++17 -w $arch $defs $incs -c "$w/$source" -o "$work/w$n.o"
done
rm -f "$out"
$ar rcs "$out" "$work"/*.o
echo "whisper: $out ($n files)"
