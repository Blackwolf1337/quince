# Maintenance

What to do when YouTube changes something, and the development tools.

## Stream puzzles

Streams depend on YouTube's player puzzles. The solver is the same code yt-dlp ships (yt-dlp-ejs). To update it, copy `lib.min.js` and `core.min.js` of a newer yt-dlp-ejs release into `third_party/ejs/` and rebuild. Without a rebuild, copy them into `~/.local/share/quince/ejs/`, where they take precedence.

## Page layout

Page layout changes are handled in `src/ytm/Parser.cpp`.

## Development tools

There are two development tools. `build/quince-parsedump` prints what the parser makes of a saved InnerTube answer:

```
quince-parsedump browse|search|continuation|suggest|next|nextmore <answer.json> [browseId]
```

`build/quince-romanize` prints the romanization of the lines given as arguments:

```
$ build/quince-romanize "夜に駆ける"
language: ja
夜に駆ける
  yoru ni kakeru
```

## Making a release

Run `tools/release.sh <version>` (for example `tools/release.sh 0.2.0`) on a clean master. It sets the version in `CMakeLists.txt`, tags `v<version>` and pushes master and the tag to Gitea, which mirrors them to GitHub. There the build workflow (`.github/workflows/build.yml`) makes the release with the Windows zip, the macOS zip and the Arch tarball.

The script then waits for GitHub's source archive of the tag, writes its checksum into the Homebrew formula (`packaging/homebrew/quince.rb`) and the AUR recipes (`packaging/aur`), and commits and pushes that. It also copies the formula into the tap repository (a fresh clone of the tap repository in `TAP_URL`, or the folder in `TAP_DIR`), which Gitea mirrors to `github.com/Blackwolf1337/homebrew-quince`. The tap's own workflow then builds the formula on macOS and runs `brew test`.

The script does not push the AUR. `quince-bin` has to be rebuilt (a new release, or `pkgrel` raised after a new build) when Arch moves ICU, mpv or FFmpeg to new sonames.
