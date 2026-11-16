# Contributing to Quince

Thanks for helping. Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first. Security problems are not reported here, see [SECURITY.md](SECURITY.md).

## Building

[docs/building.md](docs/building.md) lists the requirements and the Windows and macOS builds. On Linux:

```
git clone https://github.com/Blackwolf1337/quince.git
cd quince
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/quince
```

## Code style

Quince is C++23 with Qt 6 and Slint. Match the surrounding file.

- Braces on their own line for functions and classes, 4 spaces of indent.
- Members are `m_name`, statics are `s_name`, constants are `kName`.
- Each area has its own logging category: `Q_LOGGING_CATEGORY(lcPlayer, "quince.player")`.
- The build uses `-Wall -Wextra`. Keep the warnings clean.
- No duplicated code. Look for an existing helper first.
- Comments only where the code is not obvious.
- The `.slint` files in `ui/` follow the style of the files around them.

## Branches

The repo has only `master`. It holds released states, and each release is tagged `vX.Y.Z`. Make a branch `feature/<name>` or `fix/<name>` from `master` and send the pull request against `master`.

## Commits

One topic per commit. The summary is one plain sentence that says what changed, often "Area: what changed". For example:

```
Own playlists: remove a track from the playlist's page, rename and delete from the sidebar's menu
Carousels: cards keep their own width in a row of albums and videos
```

## Pull requests

Say what changed and why, how you tested it, which systems you built on and what you did not test. Keep them small. The pull request template asks for these.

When texts change, update the German catalogs in `i18n/`. See "Translations" in [docs/building.md](docs/building.md).

## YouTube changes

Quince depends on YouTube's internal API. How the solver and the parser are updated is in [docs/maintenance.md](docs/maintenance.md).

## Questions

Open an [issue](https://github.com/Blackwolf1337/quince/issues).
