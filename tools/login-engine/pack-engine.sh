#!/usr/bin/env bash
# The login engine in XPS5's installer (phase 5, 2026-11-13): build-login.sh's
# engine-files.tar, packed with xz, and the SDK's liblzma for the installer
# to unpack it on the console (src/install.c, src/tarout.c), into the
# project's build/engine for build.ps1:
#   engine-files.tar.xz   the engine's 9 files (142 MiB) packed
#   engine-files.sha256   the tar's SHA-256: the installer writes the engine
#                         again only when it differs from the one there
#   lib/liblzma.a, include/lzma.h, include/lzma/
# build.ps1 runs it with -Wsl. xz packs again only when the tar is newer.
#
# Run inside WSL:  bash tools/login-engine/pack-engine.sh
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=${XPS5_WORK:-$HOME/xps5-build}
TAR=$WORK/login/engine-files.tar
OUT=$ROOT/build/engine
SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
HB=$SDK/target/user/homebrew

[[ -f "$TAR" ]] || { echo "error: $TAR not found (build-engine.sh build, then build-login.sh)" >&2; exit 1; }
[[ -f "$HB/lib/liblzma.a" && -f "$HB/include/lzma.h" ]] || { echo "error: no liblzma in $HB" >&2; exit 1; }
mkdir -p "$OUT/lib" "$OUT/include/lzma"

if [[ ! -f "$OUT/engine-files.tar.xz" || "$TAR" -nt "$OUT/engine-files.tar.xz" ]]; then
    # 8 MiB of dictionary (-6): the console unpacks it with about 9 MiB
    xz -6e -T0 -c "$TAR" > "$OUT/engine-files.tar.xz.new"
    mv "$OUT/engine-files.tar.xz.new" "$OUT/engine-files.tar.xz"
fi
xz -t "$OUT/engine-files.tar.xz"
sha256sum "$TAR" | cut -d' ' -f1 > "$OUT/engine-files.sha256"
cp "$HB/lib/liblzma.a" "$OUT/lib/"
cp "$HB/include/lzma.h" "$OUT/include/"
cp "$HB"/include/lzma/*.h "$OUT/include/lzma/"
echo "engine: $(stat -c %s "$TAR") bytes, packed $(stat -c %s "$OUT/engine-files.tar.xz"), sha256 $(cat "$OUT/engine-files.sha256")"
