#!/usr/bin/env bash
# The ICU data of XPS5's login engine (2026-11-13): v1's (ICU 74.2, the
# prebuilt icudt74l.dat of its source, 30.6 MB) with what a login page
# never needs taken out, into a libicudata.a of its own. v1's ICU folder is
# only read; nothing goes into the SDK sysroot.
#
# Out: the old encodings' tables (*.cnv: WebKit decodes UTF-8, Latin-1 and
# the other encodings of the web with its own codecs), transliteration,
# spelled-out numbers (rbnf), the characters' names, the word dictionaries
# of the languages written without spaces, and every locale but XPS5's
# seven (en, pt, es, fr, de, it, nl) and root. A locale that is gone falls
# back to root. In: everything else (normalization and IDNA, properties,
# collation's root, the break rules, time zones, plurals...).
#
# Run inside WSL:  bash tools/login-engine/build-icu-data.sh
set -euo pipefail

V1_WORK=${V1_WORK:-$HOME/browserps5-build}
WORK=${XPS5_WORK:-$HOME/xps5-build}
ICU=$V1_WORK/icu
OUT=$WORK/icu-data
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
KEEP_LANGS="en pt es fr de it nl"

export LD_LIBRARY_PATH=$ICU/host/lib
icupkg=$ICU/host/bin/icupkg
genccode=$ICU/host/bin/genccode
dat=$ICU/src/source/data/in/icudt74l.dat

rm -rf "$OUT"
mkdir -p "$OUT"
"$icupkg" -l "$dat" > "$OUT/all.txt"

# what goes: one item per line, as icupkg lists them
python3 -I - "$OUT/all.txt" "$OUT/remove.txt" $KEEP_LANGS <<'EOF'
import re, sys
items = open(sys.argv[1]).read().split()
keep_langs = set(sys.argv[3:])
locale = re.compile(r"^([a-z]{2,3})(_[A-Za-z0-9]+)*\.res$")
always = {"root.res", "pool.res", "res_index.res"}
gone = []
for item in items:
    folder, _, name = item.rpartition("/")
    top = folder.split("/")[0] if folder else ""
    if name.endswith(".cnv") or name == "unames.icu" or name.endswith(".dict") or \
       name.endswith(".cfu") or top in ("translit", "rbnf"):
        gone.append(item)
        continue
    if name in always:
        continue
    m = locale.match(name)
    if m and name != "res_index.res" and m.group(1) not in keep_langs:
        gone.append(item)
open(sys.argv[2], "w").write("\n".join(gone) + "\n")
print(f"{len(items)} items, {len(gone)} out")
EOF

# --ignore-deps: brkitr/root.res names the dictionaries, and some locales
# name a parent or an alias that is gone. ICU copes at run time (the same
# as its own "brkitr_dictionaries" filter): a script written without spaces
# is then broken per character, a missing locale falls back to root.
"$icupkg" --ignore-deps -r "$OUT/remove.txt" "$dat" "$OUT/icudt74l.dat"
echo "data: $(stat -c %s "$dat") -> $(stat -c %s "$OUT/icudt74l.dat") bytes"

# into a static library with v1's symbol (icudt74_dat), built for the PS5
"$genccode" -a gcc -e icudt74 -d "$OUT" "$OUT/icudt74l.dat"
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
asm=$(ls "$OUT"/*.S | head -1)
"$CC" -c -fPIC -o "$OUT/icudt74l_dat.o" "$asm"
rm -f "$OUT/libicudata.a"
"$AR" rcs "$OUT/libicudata.a" "$OUT/icudt74l_dat.o"
llvm-nm-20 -S "$OUT/libicudata.a" 2>/dev/null | grep icudt || nm -S "$OUT/libicudata.a" | grep icudt
echo "library: $(stat -c %s "$OUT/libicudata.a") bytes (v1's $(stat -c %s "$ICU/ps5/lib/libicudata.a"))"
