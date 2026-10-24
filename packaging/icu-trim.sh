#!/bin/bash
# ICU's data cut down to what Qt and Quince use: the transliterators of the
# lyrics' romanization (src/lyrics/Romanizer.cpp: Han-Latin, Hans-Hant,
# Hant-Hans), the shared data and the root, English and German locales. Out go
# the converters, collation, word dictionaries and the names of languages,
# regions, currencies, units and time zones (33 MB to 3). For the platforms'
# trim-icu.sh, which take the data out of the packaged library and put it back.
# usage: icu-trim.sh <in.dat> <out.dat>
set -euo pipefail
in=$1
out=$2
icupkg=$(command -v icupkg || echo "$(brew --prefix icu4c)/bin/icupkg")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# icupkg wants the file named like the package in it (icudt78l)
package=$(basename "$in" .dat)
"$icupkg" -l "$in" > "$work/items"
# locale bundles look like de.res, sr_Latn_BA.res; the other top-level ones
# (zoneinfo64, plurals, pool, ...) are shared data
grep -E '^(brkitr|coll|curr|lang|rbnf|region|unit|zone)/|\.cnv$|^[a-z]{2,3}(_[A-Za-z0-9]+)*\.res$' "$work/items" \
    | grep -vxE 'en\.res|de\.res' > "$work/remove.txt"
mkdir "$work/out"
"$icupkg" -r "$work/remove.txt" "$in" "$work/out/$package.dat"
cp "$work/out/$package.dat" "$out"
echo "ICU data: $(wc -l < "$work/remove.txt") of $(wc -l < "$work/items") items out, $(wc -c < "$in") -> $(wc -c < "$out") bytes"
