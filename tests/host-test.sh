#!/bin/sh
# Builds and runs the host tests with the address and undefined-behaviour
# sanitizers: tests/xapi_test.c (JSON, session, timeline, certificates),
# tests/xtid_test.c (the x-client-transaction-id header), tests/xnotify_test.c
# (which X notifications are new, and their PS5 toasts), tests/mp4_test.c
# (the samples of an MP4), tests/login_test.c (the title's side of the login
# window), tests/qr_test.c (the QR codes), tests/trophy_test.c
# (the PS5's trophy files), tests/update_test.c (XPS5's updates), tests/play_test.c (Playing now and
# invites), tests/title_test.c (the title's text, shapes,
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

${CC:-cc} $flags -I "$dir/src" -I "$out" -o "$out/link_test" "$dir/tests/link_test.c" \
  "$dir/src/link.c" "$dir/src/xapi.c" "$dir/src/play.c" "$dir/src/json.c" "$dir/src/tls.c" "$dir/src/mem.c"
"$out/link_test"

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
  "$dir/src/xnotify.c" "$dir/src/json.c" "$dir/src/mem.c"
"$out/xnotify_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/mp4_test" "$dir/tests/mp4_test.c" "$dir/src/mp4.c"
"$out/mp4_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/qr_test" "$dir/tests/qr_test.c" "$dir/src/qr.c"
"$out/qr_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/trophy_test" "$dir/tests/trophy_test.c" "$dir/src/trophy.c" \
  "$dir/src/json.c" "$dir/src/mem.c"
"$out/trophy_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/update_test" "$dir/tests/update_test.c" "$dir/src/update.c" \
  "$dir/src/json.c" "$dir/src/mem.c"
"$out/update_test"

${CC:-cc} $flags -I "$dir/src" -o "$out/play_test" "$dir/tests/play_test.c" "$dir/src/play.c"
"$out/play_test"

${CC:-cc} $flags -I "$dir/title" -I "$dir/src" -I "$dir/third_party/stb" -DCAPTURES_HOST_DIR -o "$out/title_test" \
  "$dir/tests/title_test.c" "$dir/title/text.c" "$dir/title/draw.c" "$dir/title/images.c" \
  "$dir/title/workers.c" "$dir/title/lang.c" "$dir/src/jpeg.c" "$dir/src/settings.c" "$dir/src/captures.c" \
  "$dir/src/game.c" "$dir/src/json.c" "$dir/src/mem.c" -lm -lpthread
rm -rf "$out/av_contents"
"$out/title_test" "$dir/title/fonts/DejaVuSans.ttf" "$dir/title/fonts/Inter-Regular.ttf" "$out/av_contents"

python3 "$dir/tools/embed.py" "$out/fonts.h" \
  "inter_regular=$dir/title/fonts/Inter-Regular.ttf" "inter_bold=$dir/title/fonts/Inter-Bold.ttf" \
  "dejavu_sans=$dir/title/fonts/DejaVuSans.ttf" "dejavu_sans_bold=$dir/title/fonts/DejaVuSans-Bold.ttf" \
  "xps5_logo=$dir/title/art/logo.png"
${CC:-cc} $flags -I "$dir/title" -I "$dir/src" -I "$dir/third_party/stb" -I "$out" \
  -o "$out/preview" "$dir/tests/preview.c" "$dir/title/ui.c" "$dir/title/draw.c" "$dir/title/workers.c" \
  "$dir/title/images.c" "$dir/title/text.c" "$dir/title/lang.c" "$dir/src/settings.c" \
  "$dir/src/game.c" "$dir/src/qr.c" "$dir/src/play.c" "$dir/src/json.c" "$dir/src/mem.c" -lm -lpthread
mkdir "$out/screens"
"$out/preview" "$out/screens" > /dev/null
echo "the screen draws"
