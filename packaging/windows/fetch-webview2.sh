#!/bin/bash
# The WebView2 SDK (NuGet Microsoft.Web.WebView2, BSD-3-Clause) for the
# sign-in window: its headers and WebView2Loader.dll, unpacked into <dir>.
# usage: fetch-webview2.sh <dir>
set -euo pipefail
dir=$1
VERSION=1.0.4314.35
SHA256=90778deb1b77e36292828b4b5833c9255c27533f40e0ddd55891ae718944edc4
[ "$(cat "$dir/.version" 2>/dev/null)" = "$VERSION" ] && exit 0
rm -rf "$dir"
mkdir -p "$dir"
curl -sSfL -o "$dir/webview2.nupkg" \
    "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$VERSION/microsoft.web.webview2.$VERSION.nupkg"
echo "$SHA256  $dir/webview2.nupkg" | sha256sum -c --quiet
bsdtar -xf "$dir/webview2.nupkg" -C "$dir" LICENSE.txt build/native/include build/native/x64/WebView2Loader.dll
rm "$dir/webview2.nupkg"
echo "$VERSION" > "$dir/.version"
