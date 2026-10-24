#!/bin/bash
# Shrinks ICU's data DLL in the portable folder to what Qt and Quince use
# (packaging/icu-trim.sh). The data comes out of MSYS2's own DLL, so it always
# matches its ICU.
# usage: trim-icu.sh <dist folder>
set -euo pipefail
dist=$1
host=x86_64-w64-mingw32
dll=$(cd "$dist" && ls libicudt*.dll)
symbol="icudt${dll//[!0-9]/}_dat"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# named like the package in it (icudt78l), as icupkg wants
package="icudt${dll//[!0-9]/}l"
mkdir "$work/in" "$work/out"
# the data behind the DLL's only export, to the end of its section
python3 - "$dist/$dll" "$symbol" "$work/in/$package.dat" <<'EOF'
import struct, sys
path, symbol, out = sys.argv[1:4]
pe = open(path, "rb").read()
coff = struct.unpack_from("<I", pe, 0x3C)[0] + 4
sections, optional_size = struct.unpack_from("<H", pe, coff + 2)[0], struct.unpack_from("<H", pe, coff + 16)[0]
optional = coff + 20
export_rva = struct.unpack_from("<I", pe, optional + 112)[0]  # PE32+: data directory 0
table = []
for i in range(sections):
    s = optional + optional_size + 40 * i
    vsize, va, raw_size, raw = struct.unpack_from("<IIII", pe, s + 8)
    table.append((va, max(vsize, raw_size), raw, raw_size))
def offset(rva):
    for va, size, raw, raw_size in table:
        if va <= rva < va + size:
            return rva - va + raw, raw + raw_size
    raise SystemExit("no section for rva %x" % rva)
e, _ = offset(export_rva)
names, functions, name_table, ordinals = struct.unpack_from("<IIIII", pe, e + 20)[1:]
for i in range(names):
    name_at, _ = offset(struct.unpack_from("<I", pe, offset(name_table)[0] + 4 * i)[0])
    name = pe[name_at:pe.index(b"\0", name_at)].decode()
    if name == symbol:
        ordinal = struct.unpack_from("<H", pe, offset(ordinals)[0] + 2 * i)[0]
        start, end = offset(struct.unpack_from("<I", pe, offset(functions)[0] + 4 * ordinal)[0])
        open(out, "wb").write(pe[start:end])
        break
else:
    raise SystemExit(symbol + " not exported")
EOF

"$(dirname "$0")/../icu-trim.sh" "$work/in/$package.dat" "$work/out/$package.dat"

# a DLL with the same export (data, 16-byte aligned as ICU wants it)
cat > "$work/icudt.c" <<EOF
__asm__(".section .rdata,\"dr\"\n.balign 16\n.globl $symbol\n$symbol:\n.incbin \"$work/out/$package.dat\"\n"
        ".section .drectve\n.ascii \" -export:$symbol,data\"\n");
EOF
$host-gcc -shared -s -o "$work/$dll" "$work/icudt.c"
cp "$work/$dll" "$dist/$dll"
