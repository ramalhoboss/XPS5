#!/bin/sh
# Draws the title's screen into PNG files on a computer, with made-up posts
# (tests/preview.c). Linux or WSL:
#   sh tests/preview.sh <out-dir>
set -e

dir=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: preview.sh <out-dir>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 "$dir/tools/embed.py" "$tmp/fonts.h" \
  "inter_regular=$dir/title/fonts/Inter-Regular.ttf" "inter_bold=$dir/title/fonts/Inter-Bold.ttf" \
  "dejavu_sans=$dir/title/fonts/DejaVuSans.ttf" "dejavu_sans_bold=$dir/title/fonts/DejaVuSans-Bold.ttf" \
  "xps5_logo=$dir/title/art/logo.png"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -I "$dir/title" -I "$dir/src" \
  -I "$dir/third_party/stb" -I "$tmp" -o "$tmp/preview" "$dir/tests/preview.c" \
  "$dir/title/ui.c" "$dir/title/draw.c" "$dir/title/workers.c" "$dir/title/images.c" "$dir/title/text.c" \
  "$dir/title/lang.c" "$dir/src/settings.c" "$dir/src/game.c" "$dir/src/qr.c" "$dir/src/play.c" "$dir/src/json.c" \
  "$dir/src/mem.c" -lm -lpthread
mkdir -p "$out"
"$tmp/preview" "$out"
