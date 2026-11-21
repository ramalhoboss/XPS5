#!/bin/sh
# XPS5's promotional video (tests/promo.c): its own screens with made-up
# posts, 1920x1080 at 60 frames a second, raw RGBA on stdout, for ffmpeg.
# Linux or WSL:
#   sh tests/promo.sh | ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 60 -i - \
#     -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -movflags +faststart xps5.mp4
# PROMO_SHOTS=<dir> writes a frame a second into PNG files there instead;
# PROMO_LINK=<text> is the last card's line about where to get XPS5.
set -e

dir=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 "$dir/tools/embed.py" "$tmp/fonts.h" \
  "inter_regular=$dir/title/fonts/Inter-Regular.ttf" "inter_bold=$dir/title/fonts/Inter-Bold.ttf" \
  "dejavu_sans=$dir/title/fonts/DejaVuSans.ttf" "dejavu_sans_bold=$dir/title/fonts/DejaVuSans-Bold.ttf" \
  "xps5_logo=$dir/title/art/logo.png" "xps5_logo_full=$dir/title/art/logo-full.png" >&2
python3 "$dir/tools/embed.py" "$tmp/promo_art.h" "pic0_png=$dir/title/art/pic0.png" >&2
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -I "$dir/title" -I "$dir/src" \
  -I "$dir/third_party/stb" -I "$dir/tests" -I "$tmp" -DCAPTURES_HOST_DIR -o "$tmp/promo" "$dir/tests/promo.c" \
  "$dir/title/ui.c" "$dir/title/draw.c" "$dir/title/workers.c" "$dir/title/images.c" "$dir/title/text.c" \
  "$dir/title/lang.c" "$dir/title/cards.c" "$dir/src/sessions.c" "$dir/src/captures.c" "$dir/src/settings.c" \
  "$dir/src/game.c" "$dir/src/qr.c" "$dir/src/play.c" "$dir/src/seen.c" "$dir/src/json.c" "$dir/src/mem.c" -lm -lpthread >&2
"$tmp/promo"
