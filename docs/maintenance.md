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
