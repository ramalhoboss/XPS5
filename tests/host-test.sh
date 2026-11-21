#!/bin/sh
# Builds and runs the host tests with the address and undefined-behaviour
# sanitizers: tests/xapi_test.c (JSON, session, timeline, certificates),
# tests/https_test.c (streams that break off and go on), tests/xtid_test.c (the x-client-transaction-id header), tests/xnotify_test.c
# (which X notifications are new, and their PS5 toasts), tests/xcrypt_test.c
# (X Chat's cryptography, against Juicebox's and OpenSSL's answers), tests/mp4_test.c
# (the samples of an MP4), tests/login_test.c (the title's side of the login
# window), tests/qr_test.c (the QR codes), tests/trophy_test.c
# (the PS5's trophy files), tests/update_test.c (XPS5's updates), tests/play_test.c (Playing now and
# invites), tests/seen_test.c (the friends as XPS5 saw them around), tests/dictate_test.c and
# tests/dictserve_test.c (dictation with whisper.cpp and models/whisper/ggml-tiny-q8_0.bin, and the
# payload that runs it), tests/title_test.c (the title's text, shapes,
# pictures, JPEGs, settings, captures and languages), and tests/preview.c,
# which draws the whole screen once. Linux or WSL:
#   sh tests/host-test.sh          (CC=clang-18 sh tests/host-test.sh to pick a compiler)
set -e

dir=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
flags="-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all"

python3 "$dir/tools/embed.py" "$out/certs.h" \
  "isrg_root_x1_pem=$dir/certs/isrg-root-x1.pem" "isrg_root_x2_pem=$dir/certs/isrg-root-x2.pem" \
  "le_root_yr_pem=$dir/certs/le-root-yr-by-x1.pem" "le_yr1_pem=$dir/certs/le-yr1.pem" \
  "le_yr2_pem=$dir/certs/le-yr2.pem"
${CC:-cc} $flags -I "$dir/src" -I "$out" -o "$out/xapi_test" \
  "$dir/tests/xapi_test.c" "$dir/src/xapi.c" "$dir/src/play.c" "$dir/src/json.c" "$dir/src/tls.c" "$dir/src/mem.c"
"$out/xapi_test" "$dir/tests/timeline.json" "$dir/tests/timeline_rich.json"

${CC:-cc} $flags -I "$dir/src" -I "$out" -o "$out/https_test" \
  "$dir/tests/https_test.c" "$dir/src/https.c" "$dir/src/tls.c" "$dir/src/mem.c"
"$out/https_test"

${CC:-cc} $flags -I "$dir/src" -I "$out" -o "$out/link_test" "$dir/tests/link_test.c" \
  "$dir/src/link.c" "$dir/src/xapi.c" "$dir/src/play.c" "$dir/src/json.c" "$dir/src/tls.c" "$dir/src/mem.c"
"$out/link_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/phone_test" "$dir/tests/phone_test.c" "$dir/src/phone.c" \
  "$dir/src/json.c" "$dir/src/mem.c"
"$out/phone_test"

${CC:-cc} $flags -D_DEFAULT_SOURCE -I "$dir/title" -I "$dir/src" -o "$out/login_test" "$dir/tests/login_test.c" \
  "$dir/title/loginlink.c" "$dir/src/mem.c" -lpthread
TMPDIR="$out" "$out/login_test"

${CC:-cc} $flags -D_DEFAULT_SOURCE -I "$dir/src" -o "$out/tarout_test" "$dir/tests/tarout_test.c" "$dir/src/tarout.c"
TMPDIR="$out" "$out/tarout_test"

${CC:-cc} $flags -D_DEFAULT_SOURCE -I "$dir/src" -o "$out/rmtree_test" "$dir/tests/rmtree_test.c" "$dir/src/rmtree.c"
TMPDIR="$out" "$out/rmtree_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/xtid_test" "$dir/tests/xtid_test.c" "$dir/src/xtid.c" -lm
"$out/xtid_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/xnotify_test" "$dir/tests/xnotify_test.c" \
  "$dir/src/xnotify.c" "$dir/src/play.c" "$dir/src/json.c" "$dir/src/mem.c"
"$out/xnotify_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/xchat_test" "$dir/tests/xchat_test.c" \
  "$dir/src/xchat.c" "$dir/src/xnotify.c" "$dir/src/play.c" "$dir/src/json.c" "$dir/src/mem.c"
"$out/xchat_test"

# X Chat's cryptography, with the parts of libsodium and BearSSL it takes
sh "$dir/tools/build-crypto.sh" ${CC:-cc} -O1 -g -fsanitize=address,undefined -- ar "$out/libxps5crypto.a" > /dev/null
crypto="-I $dir/third_party/libsodium/src/include -I $dir/third_party/bearssl/inc"
${CC:-cc} $flags -I "$dir/src" $crypto -o "$out/xcrypt_test" "$dir/tests/xcrypt_test.c" "$dir/src/xcrypt.c"   "$out/libxps5crypto.a"
"$out/xcrypt_test"

# the recovery of the X Chat keys from Juicebox's realms
${CC:-cc} $flags -I "$dir/src" $crypto -o "$out/juicebox_test" "$dir/tests/juicebox_test.c" \
  "$dir/src/juicebox.c" "$dir/src/xcrypt.c" "$dir/src/json.c" "$dir/src/mem.c" "$out/libxps5crypto.a"
"$out/juicebox_test"

# opening a conversation's key and its messages (the hooks of xchat.c)
${CC:-cc} $flags -I "$dir/src" $crypto -o "$out/xchatopen_test" "$dir/tests/xchatopen_test.c" \
  "$dir/src/xchatopen.c" "$dir/src/xchat.c" "$dir/src/xcrypt.c" "$dir/src/json.c" "$dir/src/mem.c" \
  "$out/libxps5crypto.a" -lpthread
TMPDIR="$out" "$out/xchatopen_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/mp4_test" "$dir/tests/mp4_test.c" "$dir/src/mp4.c"
"$out/mp4_test"

# a clip cut out of a capture without encoding it again
${CC:-cc} $flags -I "$dir/src" -o "$out/clip_test" "$dir/tests/clip_test.c" "$dir/src/clip.c" "$dir/src/mp4.c"
"$out/clip_test"

# a Space's sound: HLS playlists, ID3 tags and ADTS frames
${CC:-cc} $flags -I "$dir/src" -o "$out/hls_test" "$dir/tests/hls_test.c" "$dir/src/hls.c"
"$out/hls_test"

# a Space handed by the title to the payload: the lines between them
${CC:-cc} $flags -I "$dir/src" -o "$out/spacecmd_test" "$dir/tests/spacecmd_test.c" "$dir/src/spacecmd.c"
"$out/spacecmd_test"

# dictation without the internet: whisper.cpp (optimised, as it is slow
# under the sanitizers) with the tiny model on whisper.cpp's own sample;
# without the model (models/whisper/ggml-tiny-q8_0.bin, which tools/whisper-q8.sh
# makes from ggml-tiny.bin, a download) it is skipped
cxx=${CXX:-$(echo "${CC:-cc}" | sed 's/clang/clang++/;s/gcc/g++/;s/^cc$/c++/')}
sh "$dir/tools/build-whisper.sh" ${CC:-cc} -O2 -- $cxx -O2 -- ar "$out/libxps5whisper.a" > /dev/null
whisper="-I $dir/third_party/whisper.cpp/include -I $dir/third_party/whisper.cpp/ggml/include"
${CC:-cc} $flags -I "$dir/src" $whisper -c "$dir/src/dictate.c" -o "$out/dictate.o"
${CC:-cc} $flags -I "$dir/src" -c "$dir/tests/dictate_test.c" -o "$out/dictate_test.o"
$cxx -fsanitize=address,undefined -o "$out/dictate_test" "$out/dictate_test.o" "$out/dictate.o" \
  "$out/libxps5whisper.a" -lpthread -lm
"$out/dictate_test" "$dir/models/whisper/ggml-tiny-q8_0.bin" "$dir/third_party/whisper.cpp/samples/jfk.wav"
# and the payload that runs it for the title, asked as the title asks
${CC:-cc} $flags -I "$dir/src" $whisper -c "$dir/src/dictserve.c" -o "$out/dictserve.o"
${CC:-cc} $flags -I "$dir/src" -c "$dir/tests/dictserve_test.c" -o "$out/dictserve_test.o"
$cxx -fsanitize=address,undefined -o "$out/dictserve_test" "$out/dictserve_test.o" "$out/dictserve.o" \
  "$out/dictate.o" "$out/libxps5whisper.a" -lpthread -lm
"$out/dictserve_test" "$dir/models/whisper/ggml-tiny-q8_0.bin" "$dir/third_party/whisper.cpp/samples/jfk.wav"

${CC:-cc} $flags -I "$dir/src" -o "$out/qr_test" "$dir/tests/qr_test.c" "$dir/src/qr.c"
"$out/qr_test"

# the voice notes' MP4, with the AAC tables of FFmpeg's source (the title
# takes them from libavcodec)
ffsrc=${FFMPEG_SRC:-$HOME/browserps5-build/media/ffmpeg-8.1.3}
if [ -f "$ffsrc/libavcodec/aactab.c" ]; then
  python3 "$dir/tools/aac_tables.py" "$ffsrc/libavcodec/aactab.c" "$out/aac_tables.c"
  ${CC:-cc} $flags -I "$dir/src" -o "$out/voice_test" "$dir/tests/voice_test.c" "$out/aac_tables.c" \
    "$dir/src/mp4.c" "$dir/src/mem.c" -lm
  "$out/voice_test"
else
  echo "voice: not checked, no FFmpeg source for the AAC tables (FFMPEG_SRC=<its folder>)"
fi

${CC:-cc} $flags -I "$dir/src" -o "$out/trophy_test" "$dir/tests/trophy_test.c" "$dir/src/trophy.c" \
  "$dir/src/json.c" "$dir/src/mem.c"
"$out/trophy_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/sessions_test" "$dir/tests/sessions_test.c" "$dir/src/sessions.c"
"$out/sessions_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/update_test" "$dir/tests/update_test.c" "$dir/src/update.c" \
  "$dir/src/json.c" "$dir/src/mem.c"
"$out/update_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/play_test" "$dir/tests/play_test.c" "$dir/src/play.c"
"$out/play_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/seen_test" "$dir/tests/seen_test.c" "$dir/src/seen.c"
"$out/seen_test"

${CC:-cc} $flags -I "$dir/title" -I "$dir/src" -I "$dir/third_party/stb" -DCAPTURES_HOST_DIR -o "$out/title_test" \
  "$dir/tests/title_test.c" "$dir/title/text.c" "$dir/title/draw.c" "$dir/title/images.c" "$dir/title/jxr.c" \
  "$dir/title/cards.c" "$dir/src/sessions.c" \
  "$dir/title/workers.c" "$dir/title/lang.c" "$dir/src/jpeg.c" "$dir/src/ultrahdr.c" "$dir/src/settings.c" \
  "$dir/src/captures.c" "$dir/src/game.c" "$dir/src/json.c" "$dir/src/mem.c" -lm -lpthread
rm -rf "$out/av_contents"
"$out/title_test" "$dir/title/fonts/DejaVuSans.ttf" "$dir/title/fonts/Inter-Regular.ttf" "$out/av_contents"

python3 "$dir/tools/embed.py" "$out/fonts.h" \
  "inter_regular=$dir/title/fonts/Inter-Regular.ttf" "inter_bold=$dir/title/fonts/Inter-Bold.ttf" \
  "dejavu_sans=$dir/title/fonts/DejaVuSans.ttf" "dejavu_sans_bold=$dir/title/fonts/DejaVuSans-Bold.ttf" \
  "xps5_logo=$dir/title/art/logo.png" "xps5_logo_full=$dir/title/art/logo-full.png"
${CC:-cc} $flags -I "$dir/title" -I "$dir/src" -I "$dir/third_party/stb" -I "$out" $crypto -DCAPTURES_HOST_DIR \
  -o "$out/preview" "$dir/tests/preview.c" "$dir/title/ui.c" "$dir/title/draw.c" "$dir/title/workers.c" \
  "$dir/title/images.c" "$dir/title/text.c" "$dir/title/lang.c" "$dir/title/cards.c" "$dir/src/sessions.c" \
  "$dir/src/captures.c" "$dir/src/settings.c" "$dir/src/xchat.c" "$dir/src/xchatopen.c" "$dir/src/xcrypt.c" \
  "$dir/src/game.c" "$dir/src/qr.c" "$dir/src/play.c" "$dir/src/seen.c" "$dir/src/json.c" "$dir/src/mem.c" \
  "$out/libxps5crypto.a" -lm -lpthread
mkdir "$out/screens"
"$out/preview" "$out/screens" > /dev/null
echo "the screen draws"
