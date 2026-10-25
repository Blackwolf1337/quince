#!/bin/bash
# Takes out of Quince.app what macdeployqt brings along for Qt Widgets and
# Qt Quick apps, then every library nothing in the bundle loads any more:
#   platforminputcontexts  the virtual keyboard (QtQuick, QtQml and the rest)
#   styles                 the macOS widget style (QtWidgets)
#   tls/libqopensslbackend OpenSSL; TLS goes through Secure Transport
#   iconengines            SVG icons (QtSvg)
#   imageformats/libqpdf   PDF pictures (QtPdf); covers are JPEG, PNG, WebP
# The window is Slint's; Qt only runs the core.
# usage: prune.sh <Quince.app>
set -euo pipefail
app=$1
frameworks=$app/Contents/Frameworks

rm -rf "$app/Contents/PlugIns/platforminputcontexts" "$app/Contents/PlugIns/styles" \
    "$app/Contents/PlugIns/tls/libqopensslbackend.dylib" "$app/Contents/PlugIns/iconengines" \
    "$app/Contents/PlugIns/imageformats/libqpdf.dylib"

# what a binary loads from Frameworks: QtCore.framework, libmpv.2.dylib, ...
# The bundling tools write @executable_path/../Frameworks/, @rpath/ or, from
# one library to the next, @loader_path/ (same folder)
loads() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' \
        | sed -nE 's#^@[a-z_]+/(.*/)?Frameworks/([^/]+).*#\2#p; s#^@(rpath|loader_path)/([^/]+).*#\2#p'
}
binary_of() { # entry of Frameworks -> its Mach-O file
    case $1 in
        *.framework) echo "$frameworks/$1/Versions/A/${1%.framework}" ;;
        *) echo "$frameworks/$1" ;;
    esac
}

# macOS's bash 3.2: no associative arrays, so the set is a list in a file
used=$(mktemp)
trap 'rm -f "$used"' EXIT
todo=()
while IFS= read -r program; do todo+=("$program"); done < <(find "$app/Contents/MacOS" -type f)
while IFS= read -r plugin; do todo+=("$plugin"); done < <(find "$app/Contents/PlugIns" -name '*.dylib')
i=0
while [ $i -lt ${#todo[@]} ]; do
    while IFS= read -r entry; do
        if [ -z "$entry" ] || grep -qxF "$entry" "$used"; then
            continue
        fi
        echo "$entry" >> "$used"
        if [ -e "$(binary_of "$entry")" ]; then
            todo+=("$(binary_of "$entry")")
        fi
    done < <(loads "${todo[$i]}")
    i=$((i + 1))
done

for entry in "$frameworks"/*; do
    name=$(basename "$entry")
    if ! grep -qxF "$name" "$used"; then
        echo "prune: $name"
        rm -rf "$entry"
    fi
done

# nothing left may load what went
missing=0
while IFS= read -r file; do
    file "$file" | grep -q Mach-O || continue
    while IFS= read -r entry; do
        if [ ! -e "$frameworks/$entry" ]; then
            echo "prune: ${file#"$app"/} loads $entry, which is gone" >&2
            missing=1
        fi
    done < <(loads "$file")
done < <(find "$app/Contents" -type f \( -perm -u+x -o -name '*.dylib' \))
exit $missing
