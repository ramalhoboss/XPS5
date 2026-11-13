#!/usr/bin/env bash
# XPS5's login window engine (2026-11-13). X's login runs only in a real
# browser, so XPS5 opens X's own login page in a WebKit of its own and keeps
# the session that page gets. The engine is BrowserPS5-WebKit's v1 WebKit
# (the user's: "Sim, usa o v1"), built here for a login page only:
#   - no JIT (the interpreter is enough for one page), no media, no WebGL;
#   - a soname of its own, libWebKitXps5.so.SOVERSION, so it never meets
#     BrowserPS5's libWebKit.so.SOVERSION in /user/homebrew/lib;
#   - everything it writes in a folder of its own (XPS5_WORK, ~/xps5-build):
#     its WebKit tree (a clone sharing v1's git objects), its shims and its
#     build. v1's folders and files are only read.
# v1's build-webkit.sh does the work, with WORK pointed here and Mesa taken
# from v1's folder. It also copies three header folders into the SDK
# sysroot, as it always does for v1 (the same files).
#
# Run inside WSL:  [FULL=1] bash tools/login-engine/build-engine.sh [configure|build|size]
#   configure  the tree, CMake, and the soname in build.ninja
#   build      the ICU data of build-icu-data.sh on the link lines, XPS5's
#              own paths (own_paths) and navigator.platform (own_platform),
#              ninja (about an hour the first time on 8 cores), then size
#   size       the stripped library and the two processes, and packed
set -euo pipefail

V1=${V1:-$(cd "$(dirname "$0")/../../.." && pwd)/BrowserPS5-WebKit/poc} # beside the project folder
V1_WORK=${V1_WORK:-$HOME/browserps5-build}
WORK=${XPS5_WORK:-$HOME/xps5-build}
# FULL=1: the full engine, in its own build folder (webkit-build-full, out-full)
# beside the plain one: v1's media (GStreamer: video, MSE, the sound of Web
# Audio) and the switches of BrowserPS5 RN's full engine (browser-rn/
# build-engine-full.sh: WebGL through ANGLE on Mesa's EGL, Web Audio,
# notifications, media session, MathML), plus OffscreenCanvas as its plus
# engine has. No JIT. 2026-11-13: X refused the login window, and the probe
# on the PS5 found none of these where a Safari has them.
FULL=${FULL:-}
BUILD=$WORK/webkit-build${FULL:+-full}
FULL_SWITCHES=(-DENABLE_WEBGL=ON -DENABLE_WEB_AUDIO=ON -DENABLE_NOTIFICATIONS=ON -DENABLE_MEDIA_SESSION=ON
               -DENABLE_MATHML=ON -DENABLE_OFFSCREEN_CANVAS=ON)
REV=95075894acb3349f591612e69cb586e157acb09f # v1's (build-webkit.sh WEBKIT_REV)
SONAME=libWebKitXps5.so.SOVERSION
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
step=${1:-build}

configure() {
    mkdir -p "$WORK"
    # its own WebKit tree: v1's script checks out and patches the tree it is
    # given, so XPS5's must not be v1's. v1's tree is a partial clone (a
    # "git clone" of it asks for objects it never fetched), so this one
    # borrows its objects (alternates) and checks out the same files.
    if [[ ! -d "$WORK/WebKit/.git" ]]; then
        rm -rf "$WORK/WebKit"
        git init -q "$WORK/WebKit"
        echo "$V1_WORK/WebKit/.git/objects" > "$WORK/WebKit/.git/objects/info/alternates"
        git -C "$WORK/WebKit" sparse-checkout set --no-cone \
            '/*' '!/LayoutTests/' '!/JSTests/' '!/PerformanceTests/' '!/Websites/' '!/ManualTests/'
        git -C "$WORK/WebKit" checkout -q --force "$REV"
    fi
    WORK=$WORK BUILD=$BUILD WEBKIT_SRC=$WORK/WebKit JIT= MEDIA=${FULL:+1} IPC_TCP= HTTP2= \
        MESA_SOFT=$V1_WORK/mesa-soft/stage/user/homebrew \
        MESA_GL=$V1_WORK/mesa-browser/stage/user/homebrew/lib/libEGL.so.1 \
        CONFIGURE_ONLY=1 bash "$V1/webkit/build-webkit.sh"
    # v1's script says -DENABLE_WEBGL=OFF; CMake run again on the same
    # folder keeps every other entry of its cache
    if [[ -n "$FULL" ]]; then
        cmake -S "$WORK/WebKit" -B "$BUILD" "${FULL_SWITCHES[@]}" > "$BUILD/full-switches.log" 2>&1 ||
            { tail -30 "$BUILD/full-switches.log"; exit 1; }
        for f in WEBGL WEB_AUDIO NOTIFICATIONS OFFSCREEN_CANVAS MEDIA_SOURCE VIDEO; do
            grep -q "^#define ENABLE_$f 1\$" "$BUILD/cmakeconfig.h" ||
                { echo "cmakeconfig.h: ENABLE_$f is not 1" >&2; exit 1; }
        done
    fi
    # the soname (CMake has none for it alone: v1's plus-soname.sh does the same)
    if ! grep -q "^  SONAME = $SONAME\$" "$BUILD/build.ninja"; then
        n=$(grep -c '^  SONAME = libWebKit.so.SOVERSION$' "$BUILD/build.ninja" || true)
        [[ "$n" = 1 ]] || { echo "build.ninja: $n SONAME lines, expected 1" >&2; exit 1; }
        sed -i "s|^  SONAME = libWebKit.so.SOVERSION\$|  SONAME = $SONAME|" "$BUILD/build.ninja"
    fi
    echo "configured: $BUILD, soname $SONAME"
}

# the ICU data of build-icu-data.sh (4.6 MB, v1's is 30.8 MB) in place of the
# sysroot's, on every link line (the dependency group's), once it is built;
# ninja then links again (the command changed), and compiles nothing
point_icu() {
    local ours=$WORK/icu-data/libicudata.a theirs
    [[ -f "$ours" ]] || { echo "no $ours yet: v1's ICU data (run build-icu-data.sh)"; return; }
    # both spellings: the dependency group's, and FindICU's (ICU::data, which
    # comes first on the link lines, so the linker takes it)
    for theirs in "$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libicudata.a" \
                  "$PS5_PAYLOAD_SDK/toolchain/../target/user/homebrew/lib/libicudata.a"; do
        if grep -q -F "$theirs" "$BUILD/build.ninja"; then
            sed -i "s|$theirs|$ours|g" "$BUILD/build.ninja"
        fi
    done
    echo "ICU data: $ours"
}

# What v1's engine reads from BrowserPS5's folders, read from XPS5's own
# (ENGINE_DIR, where the engine's files go on the PS5) so the two never mix
# and the engine needs nothing BrowserPS5 put on a console:
#   - the CA list (CertificateStore.h's BROWSERPS5_CA_BUNDLE): a copy of the
#     header with XPS5's path, first on the include line of the one file that
#     reads it (CurlSSLHandlePlayStation.cpp, built alone);
#   - the settings file every process reads at its start (crash-report.c's
#     env.txt, setenv for each line): XPS5's, and none of BrowserPS5 RN's.
# The copies are made from v1's files, which are only read. ninja then
# compiles that one file again and links the three binaries again.
ENGINE_DIR=/data/TwitterPS5/engine
own_paths() {
    local own=$WORK/own n new
    local header=$V1/webkit/certificate-store/include/CertificateStore/CertificateStore.h
    local report=$V1/webkit/crash-report/crash-report.c
    mkdir -p "$own/include/CertificateStore" "$own/crash-report"

    new=$own/include/CertificateStore/CertificateStore.h
    sed "s|^#define BROWSERPS5_CA_BUNDLE \".*\"\$|#define BROWSERPS5_CA_BUNDLE \"$ENGINE_DIR/cert.pem\"|" "$header" > "$new.tmp"
    grep -q "^#define BROWSERPS5_CA_BUNDLE \"$ENGINE_DIR/cert.pem\"\$" "$new.tmp" ||
        { echo "CertificateStore.h: no BROWSERPS5_CA_BUNDLE line" >&2; exit 1; }
    cmp -s "$new.tmp" "$new" && rm "$new.tmp" || mv "$new.tmp" "$new"
    n=$(grep -c "^build Source/WebCore/CMakeFiles/WebCore.dir/platform/network/playstation/CurlSSLHandlePlayStation.cpp.o:" "$BUILD/build.ninja" || true)
    [[ "$n" = 1 ]] || { echo "build.ninja: $n CurlSSLHandlePlayStation builds, expected 1" >&2; exit 1; }
    if ! grep -q -F "INCLUDES = -I$own/include " "$BUILD/build.ninja"; then
        # the first INCLUDES line after that build line
        sed -i "/^build Source\/WebCore\/CMakeFiles\/WebCore.dir\/platform\/network\/playstation\/CurlSSLHandlePlayStation.cpp.o:/,/^  INCLUDES = /s|^  INCLUDES = |  INCLUDES = -I$own/include |" "$BUILD/build.ninja"
        [[ "$(grep -c -F "INCLUDES = -I$own/include " "$BUILD/build.ninja")" = 1 ]] ||
            { echo "build.ninja: the CA header's include went wrong" >&2; exit 1; }
    fi

    new=$own/crash-report/crash-report.c
    sed -e "s|^#define DEBUG_ENV_FILE \".*\"\$|#define DEBUG_ENV_FILE \"$ENGINE_DIR/env.txt\"|" \
        -e 's|^#define DEBUG_ENV_FILE_RN ".*"$|#define DEBUG_ENV_FILE_RN ""|' \
        -e 's|^#define ENGINE_ENV_FILE_RN ".*"$|#define ENGINE_ENV_FILE_RN ""|' "$report" > "$new.tmp"
    n=$(grep -c -e "^#define DEBUG_ENV_FILE \"$ENGINE_DIR/env.txt\"\$" -e '^#define DEBUG_ENV_FILE_RN ""$' \
                -e '^#define ENGINE_ENV_FILE_RN ""$' "$new.tmp" || true)
    [[ "$n" = 3 ]] || { echo "crash-report.c: $n of its 3 settings files changed" >&2; exit 1; }
    cmp -s "$new.tmp" "$new" && rm "$new.tmp" || mv "$new.tmp" "$new"
    # as v1's build-webkit.sh builds it (its own folder on the include line
    # for its "../libc-fixes/known_threads.h"), with the sqlite-batch.o v1's
    # script left in this WORK
    (
        source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
        "$CC" -O2 -fPIC -Wall -Werror -I"$V1/common" -I"$V1/webkit/crash-report" \
            -c "$new" -o "$WORK/crash-report/crash-report.own.o"
        rm -f "$WORK/crash-report/libCrashReport.own.a"
        "$AR" rcs "$WORK/crash-report/libCrashReport.own.a" "$WORK/crash-report/crash-report.own.o" \
            "$WORK/crash-report/sqlite-batch.o"
    )
    if ! cmp -s "$WORK/crash-report/libCrashReport.own.a" "$WORK/crash-report/libCrashReport.a"; then
        mv "$WORK/crash-report/libCrashReport.own.a" "$WORK/crash-report/libCrashReport.a"
        # ninja does not see the archive: the three are linked again
        rm -f "$BUILD/lib/libWebKit.so.SOVERSION" "$BUILD/bin/WebProcess" "$BUILD/bin/NetworkProcess"
    fi
    echo "own paths: $ENGINE_DIR (cert.pem, env.txt)"
}

# navigator.platform: v1's engine says "MacIntel", as the Mac's Safari that
# BrowserPS5 names itself; XPS5's engine names itself a PlayStation 5
# (login-ui.cpp), so its platform says the same (2026-11-13: X refused the
# login window, and the probe on the PS5 saw "MacIntel" beside a PlayStation
# name). v1's script writes the file again from the revision at each
# configure, so this runs before every build.
own_platform() {
    local file=$WORK/WebKit/Source/WebCore/page/NavigatorBase.cpp n
    sed -i 's|^    return "MacIntel"_s; // BrowserPS5: .*$|    return "PlayStation 5"_s; // XPS5 (tools/login-engine/build-engine.sh own_platform): what it runs on, as its name says|' "$file"
    n=$(grep -c -F '    return "PlayStation 5"_s; // XPS5 ' "$file" || true)
    [[ "$n" = 1 ]] || { echo "NavigatorBase.cpp: $n XPS5 platform lines, expected 1" >&2; exit 1; }
    echo "navigator.platform: PlayStation 5"
}

size() {
    local strip out=$WORK/out${FULL:+-full}
    strip=$(ls "$PS5_PAYLOAD_SDK"/bin/*strip* 2>/dev/null | head -1)
    [[ -n "$strip" ]] || strip=llvm-strip-20
    mkdir -p "$out"
    "$strip" --strip-unneeded -o "$out/$SONAME" "$BUILD/lib/libWebKit.so.SOVERSION"
    for exe in WebProcess NetworkProcess; do
        "$strip" --strip-debug -o "$out/$exe.elf" "$BUILD/bin/$exe"
        readelf -d "$out/$exe.elf" | grep -q "\[$SONAME\]" || { echo "$exe does not load $SONAME" >&2; exit 1; }
    done
    readelf -d "$out/$SONAME" | grep -i soname
    for f in "$out/$SONAME" "$out/WebProcess.elf" "$out/NetworkProcess.elf"; do
        echo "$(basename "$f"): $(stat -c %s "$f") bytes, xz $(xz -6 -T0 -c "$f" | wc -c)"
    done
}

case "$step" in
    configure) configure ;;
    build)
        [[ -f "$BUILD/build.ninja" ]] || configure
        grep -q "^  SONAME = $SONAME\$" "$BUILD/build.ninja" || configure
        point_icu
        own_paths
        own_platform
        env -u CC ninja -C "$BUILD" -j"${JOBS:-$(nproc)}" WebKit WebProcess NetworkProcess
        size ;;
    size) size ;;
    *) echo "configure, build or size" >&2; exit 1 ;;
esac
