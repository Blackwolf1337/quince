#!/bin/bash
# Takes out of Quince.app what macdeployqt brings along for Qt Widgets and
# Qt Quick apps, then every library nothing in the bundle loads any more:
#   platforminputcontexts  the virtual keyboard (QtQuick, QtQml and the rest)
#   styles                 the macOS widget style (QtWidgets)
#   tls/libqopensslbackend OpenSSL; TLS goes through Secure Transport
# The window is Slint's; Qt only runs the core.
# usage: prune.sh <Quince.app>
set -euo pipefail
app=$1
frameworks=$app/Contents/Frameworks

rm -rf "$app/Contents/PlugIns/platforminputcontexts" "$app/Contents/PlugIns/styles" \
    "$app/Contents/PlugIns/tls/libqopensslbackend.dylib"

# what a binary loads from Frameworks: QtCore.framework, libmpv.2.dylib, ...
loads() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' \
        | sed -nE 's#^(@rpath|@executable_path/\.\./Frameworks|@loader_path(/\.\.)*/Frameworks)/([^/]+).*#\3#p'
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
todo=("$app/Contents/MacOS/Quince")
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
