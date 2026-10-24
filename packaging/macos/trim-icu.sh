#!/bin/bash
# Shrinks ICU's data library in Quince.app to what Qt and Quince use
# (packaging/icu-trim.sh). The data comes out of Homebrew's own library, so it
# always matches its ICU; the new one keeps its install name and versions.
# usage: trim-icu.sh <libicudata.NN.dylib>
set -euo pipefail
lib=$1
name=$(basename "$lib")
major=${name//[!0-9]/}
symbol="icudt${major}_dat"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# named like the package in it (icudt78l), as icupkg wants
package="icudt${major}l"
mkdir "$work/in" "$work/out"
# the data behind the library's symbol, to the end of its section
python3 - "$lib" "_$symbol" "$work/in/$package.dat" <<'EOF'
import struct, sys
path, symbol, out = sys.argv[1:4]
m = open(path, "rb").read()
magic, _, _, _, ncmds = struct.unpack_from("<IiiII", m, 0)
if magic != 0xFEEDFACF:
    raise SystemExit("not a thin 64-bit Mach-O file")
at, sections, symtab = 32, [], None
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", m, at)
    if cmd == 0x19:  # LC_SEGMENT_64
        nsects = struct.unpack_from("<I", m, at + 64)[0]
        for i in range(nsects):
            s = at + 72 + 80 * i
            addr, ssize, offset = struct.unpack_from("<QQI", m, s + 32)
            sections.append((addr, ssize, offset))
    elif cmd == 0x2:  # LC_SYMTAB
        symtab = struct.unpack_from("<IIII", m, at + 8)
    at += size
symoff, nsyms, stroff, _ = symtab
for i in range(nsyms):
    strx, _, _, _, value = struct.unpack_from("<IBBHQ", m, symoff + 16 * i)
    if m[stroff + strx:m.index(b"\0", stroff + strx)].decode() == symbol:
        break
else:
    raise SystemExit(symbol + " not found")
for addr, ssize, offset in sections:
    if addr <= value < addr + ssize:
        open(out, "wb").write(m[offset + value - addr:offset + ssize])
        break
else:
    raise SystemExit("no section for " + symbol)
EOF

"$(dirname "$0")/../icu-trim.sh" "$work/in/$package.dat" "$work/out/$package.dat"

# a library with the same name, versions and symbol (16-byte aligned, as ICU
# wants its data)
id=$(otool -D "$lib" | tail -n 1)
versions=$(otool -L "$lib" | sed -n 2p)
compat=$(sed -E 's/.*compatibility version ([0-9.]+).*/\1/' <<< "$versions")
current=$(sed -E 's/.*current version ([0-9.]+).*/\1/' <<< "$versions")
printf '.section __TEXT,__const\n.globl _%s\n.p2align 4\n_%s:\n.incbin "%s"\n' \
    "$symbol" "$symbol" "$work/out/$package.dat" > "$work/icudt.s"
clang -dynamiclib -o "$work/$name" "$work/icudt.s" -install_name "$id" \
    -compatibility_version "$compat" -current_version "$current" -Wl,-x
cp "$work/$name" "$lib"
