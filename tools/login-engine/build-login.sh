#!/usr/bin/env bash
# XPS5's login window, the engine's side (phase 3, 2026-11-13), built from
# login/ after build-engine.sh:
#   xps5-login.elf     launcher.c: the payload the title sends to the ELF
#                      loader when its login window opens
#   login-ui.elf       login-ui.cpp: the window process (WebKit's UIProcess)
#   login-files.tar    those two, for xps5-engine-install.elf (build-probe.sh)
#                      over an engine already in /data/TwitterPS5/engine
#   engine-files.tar   the whole engine: the two above, libWebKitXps5, Mesa's
#                      libEGL and libvulkan (v1's build, which the engine is
#                      linked with), the two processes, Ubuntu's CA list
#                      (Mozilla's) and fonts.conf (the console's own fonts)
# Both sides take src/loginproto.h. launcher.c starts processes with v1's
# copy of elfldr (John Törnblom's, GPLv3+, read from v1's poc/common/elfldr)
# changed here in one line: the children get an environment (execve's envp)
# instead of none. v1's files are only read.
#
# Run inside WSL:  bash tools/login-engine/build-login.sh
# With FULL=1 it takes the full engine (build-engine.sh with FULL=1: WebGL,
# Web Audio, media) into ~/xps5-build/login-full. Its library and processes
# go together: the messages between them are numbered by the build's switches.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
V1=${V1:-$(cd "$(dirname "$0")/../../.." && pwd)/BrowserPS5-WebKit/poc} # beside the project folder
V1_WORK=${V1_WORK:-$HOME/browserps5-build}
WORK=${XPS5_WORK:-$HOME/xps5-build}
FULL=${FULL:-}
BUILD=$WORK/webkit-build${FULL:+-full}
OUT=$WORK/login${FULL:+-full}
ENGINE_OUT=$WORK/out${FULL:+-full}
MESA=$V1_WORK/mesa-browser/stage/user/homebrew/lib
CA_BUNDLE=${CA_BUNDLE:-/etc/ssl/certs/ca-certificates.crt}
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

need() {
    [[ -e "$1" ]] || { echo "error: $1 not found${2:+ ($2)}" >&2; exit 1; }
}
need "$ENGINE_OUT/libWebKitXps5.so.SOVERSION" "run build-engine.sh first"
need "$BUILD/lib/libWebKit.so.SOVERSION" "run build-engine.sh first"
need "$MESA/libEGL.so.1"
need "$MESA/libvulkan.so.1"
need "$CA_BUNDLE"
mkdir -p "$OUT/elfldr" "$OUT/stage"
# the engine reads XPS5's files, not BrowserPS5's (build-engine.sh own_paths);
# its strings once, in a file (grep -q on a pipe ends strings with SIGPIPE)
strings -a "$ENGINE_OUT/libWebKitXps5.so.SOVERSION" > "$OUT/engine-strings.txt"
for s in /data/TwitterPS5/engine/cert.pem /data/TwitterPS5/engine/env.txt; do
    grep -q -x -F "$s" "$OUT/engine-strings.txt" ||
        { echo "error: the engine does not read $s (build-engine.sh build)" >&2; exit 1; }
done
for s in /user/homebrew/etc/ssl/cert.pem /user/homebrew/browserps5/data/env.txt \
         /user/homebrew/browserps5-rn/data/env.txt /user/homebrew/browserps5-rn/engine-env.txt; do
    if grep -q -x -F "$s" "$OUT/engine-strings.txt"; then
        echo "error: the engine still reads $s" >&2; exit 1
    fi
done
rm -f "$OUT/engine-strings.txt" "$OUT"/stage/*

# elfldr, with an environment for the processes it starts
for f in elfldr.c elfldr.h pt.c pt.h log.h asset.h; do
    sed 's/\r$//' "$V1/common/elfldr/$f" > "$OUT/elfldr/$f" # v1's have CRLF
done
n=$(grep -c '^  execve(SceSpZeroConf, argv, 0);$' "$OUT/elfldr/elfldr.c" || true)
[[ "$n" = 1 ]] || { echo "elfldr.c: $n execve lines, expected 1" >&2; exit 1; }
sed -i -e 's|^  execve(SceSpZeroConf, argv, 0);$|  execve(SceSpZeroConf, argv, elfldr_child_envp);|' \
       -e '1i char** elfldr_child_envp; // XPS5 (build-login.sh): the environment of the processes it starts' \
       "$OUT/elfldr/elfldr.c"

"$CC" -Wall -Werror -O2 -I"$OUT/elfldr" -I"$V1/webkit/process-launcher" -I"$ROOT/src" \
    -o "$OUT/xps5-login.elf" "$HERE/login/launcher.c" "$OUT/elfldr/elfldr.c" "$OUT/elfldr/pt.c"
# login-ui reads X's two cookies from the engine's cookie jar with the SDK's
# SQLite (the one WebKit is linked with: its own copy is not exported)
"$CXX" -Wall -Werror -O2 -std=c++20 -I"$ROOT/src" \
    -I"$BUILD/WebKit/Headers" -I"$BUILD/JavaScriptCore/Headers" -I"$PS5_SYSROOT$PS5_HBROOT/include" \
    -o "$OUT/login-ui.elf" "$HERE/login/login-ui.cpp" "$BUILD/lib/libWebKit.so.SOVERSION" \
    "$PS5_SYSROOT$PS5_HBROOT/lib/libsqlite3.a"
readelf -d "$OUT/login-ui.elf" | grep -q "\[libWebKitXps5.so.SOVERSION\]" ||
    { echo "login-ui.elf does not load libWebKitXps5.so.SOVERSION" >&2; exit 1; }

strip=$(ls "$PS5_PAYLOAD_SDK"/bin/*strip* 2>/dev/null | head -1)
[[ -n "$strip" ]] || strip=llvm-strip-20
"$strip" --strip-debug -o "$OUT/stage/xps5-login.elf" "$OUT/xps5-login.elf"
"$strip" --strip-debug -o "$OUT/stage/login-ui.elf" "$OUT/login-ui.elf"
(cd "$OUT/stage" && tar --format=ustar --owner=0 --group=0 --mtime=@0 -cf "$OUT/login-files.tar" xps5-login.elf login-ui.elf)

cp "$ENGINE_OUT/libWebKitXps5.so.SOVERSION" "$ENGINE_OUT/WebProcess.elf" "$ENGINE_OUT/NetworkProcess.elf" "$OUT/stage/"
"$strip" --strip-unneeded -o "$OUT/stage/libEGL.so.1" "$(readlink -f "$MESA/libEGL.so.1")"
"$strip" --strip-unneeded -o "$OUT/stage/libvulkan.so.1" "$(readlink -f "$MESA/libvulkan.so.1")"
cp "$CA_BUNDLE" "$OUT/stage/cert.pem"
cp "$HERE/probe/fonts.conf" "$OUT/stage/fonts.conf"
(cd "$OUT/stage" && tar --format=ustar --owner=0 --group=0 --mtime=@0 -cf "$OUT/engine-files.tar" \
    libWebKitXps5.so.SOVERSION libEGL.so.1 libvulkan.so.1 WebProcess.elf NetworkProcess.elf \
    login-ui.elf xps5-login.elf cert.pem fonts.conf)
for f in "$OUT/stage"/* "$OUT/login-files.tar" "$OUT/engine-files.tar"; do
    printf '%-28s %10s bytes\n' "$(basename "$f")" "$(stat -c %s "$f")"
done
