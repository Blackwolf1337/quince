#!/bin/bash
# Makes a release: tools/release.sh 0.2.0
#
#  1. sets the version in CMakeLists.txt, commits it and tags v<version>
#  2. pushes master and the tag (Gitea mirrors both to GitHub, where the build
#     workflow makes the release with the Windows, macOS and Arch files)
#  3. waits for GitHub's source archive of the tag and puts its checksum into
#     the Homebrew formula and the AUR recipes, commits and pushes that
#  4. copies the formula into the tap (a fresh clone of TAP_URL,
#     or TAP_DIR) and pushes it
#
# Needs a clean tree on master. The AUR is not pushed (see packaging/aur).
set -euo pipefail

ver=${1:-}
if [[ ! $ver =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "usage: $0 <major.minor.patch>" >&2
	exit 1
fi
tag=v$ver
repo=$(cd "$(dirname "$0")/.." && pwd)
github=https://github.com/Blackwolf1337/quince
tap_url=${TAP_URL:-}
cd "$repo"

# the tap is pushed at the end: know where before anything is tagged
if [[ -z ${TAP_DIR:-} && -z $tap_url ]]; then
	echo "set TAP_URL to the tap repository you push to, or TAP_DIR to a clone of it" >&2
	exit 1
fi

[[ $(git branch --show-current) == master ]] || { echo "not on master" >&2; exit 1; }
[[ -z $(git status --porcelain) ]] || { echo "the tree has changes" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/$tag" > /dev/null && { echo "$tag exists already" >&2; exit 1; }
git pull -q --ff-only origin master

# 1. version and tag
sed -i "s/^project(quince VERSION [0-9.]*/project(quince VERSION $ver/" CMakeLists.txt
if ! git diff --quiet CMakeLists.txt; then
	git commit -q -m "Version $ver" CMakeLists.txt
fi
git tag -a "$tag" -m "Quince $ver"

# 2. push
git push -q origin master "$tag"
echo "pushed master and $tag; the build runs at $github/actions"

# 3. the source archive appears on GitHub once the mirror has the tag
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT
for _ in $(seq 60); do
	curl -sfL -o "$archive" "$github/archive/refs/tags/$tag.tar.gz" && break
	sleep 10
done
[[ -s $archive ]] || { echo "no source archive of $tag on GitHub after 10 minutes" >&2; exit 1; }
sum=$(sha256sum "$archive" | cut -d' ' -f1)
echo "source archive sha256 $sum"

formula=packaging/homebrew/quince.rb
sed -i -e "s|/quince/archive/refs/tags/v[0-9.]*\.tar\.gz|/quince/archive/refs/tags/$tag.tar.gz|" \
	-e "0,/^  sha256 \"[0-9a-f]*\"/s//  sha256 \"$sum\"/" "$formula"
for recipe in packaging/aur/quince/PKGBUILD packaging/aur/quince-bin/PKGBUILD; do
	sed -i -e "s/^pkgver=.*/pkgver=$ver/" -e "s/^pkgrel=.*/pkgrel=1/" "$recipe"
done
# the first checksum of quince is the source archive
sed -i "0,/^sha256sums=('[0-9a-zA-Z]*'/s//sha256sums=('$sum'/" packaging/aur/quince/PKGBUILD
git commit -q -m "Packaging: $tag checksums" "$formula" packaging/aur
git push -q origin master

# 4. the tap
tap=${TAP_DIR:-}
if [[ -z $tap ]]; then
	tap=$(mktemp -d)
	trap 'rm -f "$archive"; rm -rf "$tap"' EXIT
	git clone -q "$tap_url" "$tap"
fi
git -C "$tap" pull -q --ff-only
# the tap's copy has no first comment line
tail -n +2 "$formula" > "$tap/Formula/quince.rb"
git -C "$tap" commit -q -m "Quince $ver" Formula/quince.rb
git -C "$tap" push -q

echo "done: $github/releases/tag/$tag"
echo "the tap's test runs at https://github.com/Blackwolf1337/homebrew-quince/actions"
