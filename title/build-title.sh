#!/usr/bin/env bash
# Builds the title's eboot.bin and sce_module/libc.prx with PS5_Vulkan's
# tooling (https://github.com/mihawk-99/PS5_Vulkan), which writes the PS5
# executable layout (type 0xFE10, dynamic imports, process parameters) and the
# FSELF. Runs on Linux/WSL with clang 18 and python3.
#
# The title is the timeline screen (main.c, ui.c, draw.c, images.c, text.c,
# video.c, ime.c, lang.c, player.c, space.c, sound.c, workers.c, loginlink.c, jxr.c) with the X
# and HTTPS code it shares with the payloads (src/; loginproto.h with the login
# engine), stb_truetype and stb_image, the Inter and DejaVu fonts and the root
# certificates, embedded as C arrays. video.c comes from
# PS5_Vulkan's GPL-3.0-or-later renderer, so the eboot is under that licence.
#
# The videos are decoded by FFmpeg (LGPL 2.1 or later; H.264 and AAC
# decoders, no encoder), linked statically from a build for the PS5: its
# copy of the archives has malloc, realloc, free, posix_memalign and
# pthread_create, localtime_r and gmtime_r renamed to the functions of
# ffmem.c (see there why).
#
# The PS5's HDR screenshots (.jxr) are decoded by jxrlib (Microsoft's JPEG XR
# library, BSD 2-Clause), built by tools/jxrlib/build.sh into one archive,
# its malloc, calloc, realloc and free renamed the same way.
#
# Usage: bash title/build-title.sh <out-dir>
#   PS5VK   a PS5_Vulkan checkout with tools/setup-native-dependencies.sh done
#           (default ~/src/PS5_Vulkan)
#   WORK    build tree (default ~/src/twps/app)
#   FFMPEG  FFmpeg built for the PS5: include/ and lib/ with libavcodec.a,
#           libswresample.a and libavutil.a (default: the one in
#           ~/browserps5-build, read only)
#   JXRLIB  where libxps5jxr.a for the PS5 is (default ~/xps5-build/jxrlib);
#           built there from JXRLIB_SRC (default
#           ~/xps5-build/jxrlib-dl/jxrlib-master) when it is not

set -euo pipefail

out=${1:?usage: build-title.sh <out-dir>}
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/.." && pwd)
ps5vk=${PS5VK:-$HOME/src/PS5_Vulkan}
work=${WORK:-$HOME/src/twps/app}
ffmpeg=${FFMPEG:-$HOME/browserps5-build/media/stage-ffmpeg/user/homebrew}
jxrlib=${JXRLIB:-$HOME/xps5-build/jxrlib}
jxrlib_src=${JXRLIB_SRC:-$HOME/xps5-build/jxrlib-dl/jxrlib-master}
export PATH=/usr/lib/llvm-18/bin:$PATH

[[ -f $ffmpeg/lib/libavcodec.a && -f $ffmpeg/include/libavcodec/avcodec.h ]] || {
    echo "no FFmpeg for the PS5 in $ffmpeg (set FFMPEG)" >&2
    exit 2
}

[[ -x $ps5vk/.deps/native/ps5-payload-sdk/bin/prospero-lld ]] || {
    echo "run tools/setup-native-dependencies.sh in $ps5vk first" >&2
    exit 2
}

if [[ ! -f $jxrlib/libxps5jxr.a ]]; then
    [[ -f $jxrlib_src/jxrgluelib/JXRGlue.c ]] || {
        echo "no jxrlib in $jxrlib, nor its source in $jxrlib_src (set JXRLIB or JXRLIB_SRC)" >&2
        exit 2
    }
    PS5VK=$ps5vk sh "$root/tools/jxrlib/build.sh" "$jxrlib_src" "$jxrlib" ps5
fi

# PS5_Vulkan's build script compiles every file under src/ and copies assets/
# and probes/ into the title, so it runs in a tree of its own with only ours.
mkdir -p "$work/src"
for dir in tools tooling vendor runtime sce_sys; do
    rsync -a --delete "$ps5vk/$dir/" "$work/$dir/"
done
# PS5_Vulkan's .deps as links, with the SDK's library stubs in a directory of
# our own: the build links every stub there, and the SDK has none of the
# microphone's library (libSceAudioIn), which title/stubs makes
rm -rf "$work/.deps"
sdk="$ps5vk/.deps/native/ps5-payload-sdk"
stubs="$work/.deps/native/ps5-payload-sdk/target/lib"
mkdir -p "$stubs"
link_all() { # <from> <the entry kept out> <to>, the hidden ones too (the SDK's revision)
    for entry in "$1"/* "$1"/.[!.]*; do
        [[ -e $entry || -L $entry ]] || continue
        [[ ${entry##*/} == "$2" ]] || ln -s "$entry" "$3/${entry##*/}"
    done
}
link_all "$ps5vk/.deps" native "$work/.deps"
link_all "$ps5vk/.deps/native" ps5-payload-sdk "$work/.deps/native"
link_all "$sdk" target "$work/.deps/native/ps5-payload-sdk"
link_all "$sdk/target" lib "$work/.deps/native/ps5-payload-sdk/target"
link_all "$sdk/target/lib" "" "$stubs"
PS5_PAYLOAD_SDK="$sdk" sh "$ps5vk/tooling/prospero-clang18" -std=c11 -O2 -fPIC -Wall -Wextra -Werror \
    -c "$here/stubs/audioin_link_stub.c" -o "$work/audioin_link_stub.o"
"$sdk/bin/prospero-lld" --shared -soname libSceAudioIn.sprx -o "$stubs/libSceAudioIn.so" \
    "$work/audioin_link_stub.o"
rm -rf "$work"/src/*
cp "$here"/main.c "$here"/ui.[ch] "$here"/draw.[ch] "$here"/images.[ch] \
    "$here"/text.[ch] "$here"/video.[ch] "$here"/ime.[ch] "$here"/mic.[ch] "$here"/cards.[ch] "$here"/lang.[ch] \
    "$here"/lang_table.h "$here"/player.[ch] "$here"/space.[ch] "$here"/sound.[ch] "$here"/workers.[ch] "$here"/loginlink.[ch] \
    "$here"/jxr.[ch] "$here"/ffmem.c "$root/src/loginproto.h" "$root/src/dictproto.h" "$work/src/"
for name in json xapi xchat xtid xsign play tls https mem settings mp4 clip hls radio spacecmd captures jpeg ultrahdr game link qr update sessions seen voice xcrypt juicebox xchatopen phone; do
    cp "$root/src/$name.c" "$root/src/$name.h" "$work/src/"
done

# FFmpeg, with its memory, threads and time functions sent to ffmem.c
rm -rf "$work/ffmpeg"
mkdir -p "$work/ffmpeg/lib"
cp -r "$ffmpeg/include" "$work/ffmpeg/include"
for lib in avcodec swresample avutil; do
    llvm-objcopy --redefine-sym=malloc=ff_sys_malloc --redefine-sym=realloc=ff_sys_realloc \
        --redefine-sym=free=ff_sys_free --redefine-sym=posix_memalign=ff_sys_posix_memalign \
        --redefine-sym=pthread_create=ff_sys_pthread_create \
        --redefine-sym=localtime_r=ff_sys_localtime_r --redefine-sym=gmtime_r=ff_sys_gmtime_r \
        "$ffmpeg/lib/lib$lib.a" "$work/ffmpeg/lib/lib$lib.a"
done
# jxrlib, its memory sent to ffmem.c too
llvm-objcopy --redefine-sym=malloc=ff_sys_malloc --redefine-sym=calloc=ff_sys_calloc \
    --redefine-sym=realloc=ff_sys_realloc --redefine-sym=free=ff_sys_free \
    "$jxrlib/libxps5jxr.a" "$work/ffmpeg/lib/libxps5jxr.a"

# X Chat's cryptography (libsodium and BearSSL): one archive, its memory sent
# to ffmem.c too so Argon2's 16 MiB comes from the PS5's flexible memory, not
# the title's small heap. xcrypt.c and juicebox.c link against it.
# -DNDEBUG: the PS5 libc has no __assert, and libsodium's asserts (blake2b,
# base64, generichash) are only sanity checks, so they are compiled out
PS5_PAYLOAD_SDK="$sdk" sh "$root/tools/build-crypto.sh" \
    sh "$ps5vk/tooling/prospero-clang18" -std=c11 -O2 -fPIC -DNDEBUG -- llvm-ar "$work/libxps5crypto-raw.a" >/dev/null
llvm-objcopy --redefine-sym=malloc=ff_sys_malloc --redefine-sym=calloc=ff_sys_calloc \
    --redefine-sym=realloc=ff_sys_realloc --redefine-sym=free=ff_sys_free \
    --redefine-sym=posix_memalign=ff_sys_posix_memalign \
    "$work/libxps5crypto-raw.a" "$work/libxps5crypto.a"
rm -rf "$work/crypto"
mkdir -p "$work/crypto"
cp -r "$root/third_party/libsodium/src/include" "$work/crypto/sodium"
cp -r "$root/third_party/bearssl/inc" "$work/crypto/bearssl"

cp "$root/third_party/stb/stb_truetype.h" "$root/third_party/stb/stb_image.h" "$work/src/"
python3 "$root/tools/embed.py" "$work/src/certs.h" \
    "isrg_root_x1_pem=$root/certs/isrg-root-x1.pem" \
    "isrg_root_x2_pem=$root/certs/isrg-root-x2.pem" \
    "le_root_yr_pem=$root/certs/le-root-yr-by-x1.pem" \
    "le_yr1_pem=$root/certs/le-yr1.pem" \
    "le_yr2_pem=$root/certs/le-yr2.pem"
python3 "$root/tools/embed.py" "$work/src/fonts.h" \
    "inter_regular=$here/fonts/Inter-Regular.ttf" \
    "inter_bold=$here/fonts/Inter-Bold.ttf" \
    "dejavu_sans=$here/fonts/DejaVuSans.ttf" \
    "dejavu_sans_bold=$here/fonts/DejaVuSans-Bold.ttf" \
    "xps5_logo=$here/art/logo.png" \
    "xps5_logo_full=$here/art/logo-full.png"

# The build script keeps the compilers' warnings to itself when they pass,
# so our sources go through the same compiler first with warnings as errors.
for source in "$work"/src/*.c; do
    PS5_PAYLOAD_SDK="$ps5vk/.deps/native/ps5-payload-sdk" sh "$work/tooling/prospero-clang18" \
        -std=c11 -O2 -Wall -Wextra -Werror -fsyntax-only -I "$work/ffmpeg/include" \
        -I "$work/crypto/sodium" -I "$work/crypto/bearssl" "$source"
done

# The script only accepts PPSA identities; the eboot does not carry the title
# ID, so the build uses a stand-in and the installer ships our param.json.
python3 - "$here/param.json" "$work/param-build.json" <<'PY'
import json, sys
param = json.load(open(sys.argv[1], encoding="utf-8"))
param["titleId"] = "PPSA99999"
param["contentId"] = "IV0000-PPSA99999_00-TWITTERPS5TEST00"
# it takes a media app only with badge 2; the badge the console sees is the
# one of our param.json, which the installer ships as it is
if param.get("applicationCategoryType") == 65536:
    param["contentBadgeType"] = 2
json.dump(param, open(sys.argv[2], "w", encoding="utf-8"), indent=2)
PY

# app_crt.cpp calls ps5_fp_ieee, which lives in the SDK fork's platform
# archive; FFmpeg's archives go in the order they call each other.
PARAM_PATH="$work/param-build.json" \
APP_INCLUDE_PATHS="ffmpeg/include crypto/sodium crypto/bearssl" \
APP_STATIC_ARCHIVES="libxps5crypto.a ffmpeg/lib/libxps5jxr.a ffmpeg/lib/libavcodec.a ffmpeg/lib/libswresample.a ffmpeg/lib/libavutil.a .deps/native/ps5-payload-sdk/target/lib/libps5platform.a" \
    bash "$work/tools/build.sh" Folder

mkdir -p "$out"
cp "$work/dist/PPSA99999/eboot.bin" "$out/eboot.bin"
cp "$work/dist/PPSA99999/sce_module/libc.prx" "$out/libc.prx"
echo "title: $out/eboot.bin $out/libc.prx"
