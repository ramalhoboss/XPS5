#!/bin/sh
# The parts of libsodium and BearSSL that X Chat needs (third_party/), in one
# archive: build-crypto.sh <compiler and its flags...> -- <archiver> <out.a>
# For example: build-crypto.sh gcc -O2 -- ar build/libxps5crypto.a
# libsodium without its configure: what it would find on a 64-bit
# little-endian machine, and nothing of the system it would look for (no
# mmap, no CPU features: its portable code).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
cc=
while [ "$1" != "--" ]; do
    cc="$cc $1"
    shift
done
shift
ar=$1
out=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sodium="$here/third_party/libsodium/src"
bearssl="$here/third_party/bearssl"
n=0
for source in $(cd "$sodium" && find . -name '*.c' | sort); do
    n=$((n + 1))
    $cc -w -DCONFIGURED=1 -DNATIVE_LITTLE_ENDIAN=1 -DHAVE_TI_MODE=1 -DHAVE_INTTYPES_H=1 \
        -DHAVE_STDINT_H=1 -DSODIUM_STATIC=1 -I "$sodium/include" -I "$sodium/include/sodium" \
        -c "$sodium/$source" -o "$work/sodium$n.o"
done
for source in $(cd "$bearssl/src" && find . -name '*.c' | sort); do
    n=$((n + 1))
    $cc -w -I "$bearssl/inc" -I "$bearssl/src" -c "$bearssl/src/$source" -o "$work/bearssl$n.o"
done
rm -f "$out"
$ar rcs "$out" "$work"/*.o
echo "crypto: $out ($n files)"
