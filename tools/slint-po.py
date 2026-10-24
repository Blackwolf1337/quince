#!/usr/bin/env python3
"""Translations of the Slint frontend from the Qt ones.

The .slint files use the English source texts of the QML frontend, so their
translations come from i18n/quince_<lang>.ts. Reads the strings of the .slint
files (a .pot written by slint-tr-extractor) and writes
i18n/slint/<lang>/LC_MESSAGES/quince-slint.po for every .ts file.

usage: tools/slint-po.py <strings.pot>
  slint-tr-extractor -o strings.pot ui/*.slint   (built from Slint's tools/tr-extractor)

A text is matched in the same context (QML file name = Slint component name)
first, else in any context with the same text. Qt's %1/%n become Slint's
{}/{n}; a plural "{n} track" | "{n} tracks" matches Qt's "%n track(s)".
Texts without a translation are listed on stderr.
"""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# texts the QML frontend got from Qt's own translations (standard buttons)
QT_TEXTS = {
    "de": {"Cancel": "Abbrechen", "OK": "OK", "Close": "Schließen"},
}


def po_unquote(lines):
    return "".join(bytes(l.strip()[1:-1], "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
                   for l in lines)


def po_quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def read_pot(path):
    """[(context, msgid, msgid_plural or None, references)]"""
    entries = []
    current = {}
    key = None
    for line in path.read_text(encoding="utf-8").splitlines() + [""]:
        if not line.strip():
            if current.get("msgid") and po_unquote(current["msgid"]):
                entries.append((po_unquote(current.get("msgctxt", [])), po_unquote(current["msgid"]),
                                po_unquote(current["msgid_plural"]) if "msgid_plural" in current else None,
                                current.get("refs", [])))
            current, key = {}, None
            continue
        if line.startswith("#:"):
            current.setdefault("refs", []).append(line)
            continue
        if line.startswith("#"):
            continue
        m = re.match(r"(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+(.*)", line)
        if m:
            key = m.group(1)
            current[key] = [m.group(2)]
        elif key and line.startswith('"'):
            current[key].append(line)
    return entries


def read_ts(path):
    """{(context, source): [translations]} and {source: [translations]}"""
    by_context, by_source = {}, {}
    root = ET.parse(path).getroot()
    for context in root.iter("context"):
        name = context.findtext("name")
        for message in context.iter("message"):
            source = message.findtext("source")
            translation = message.find("translation")
            if translation is None or translation.get("type") in ("unfinished", "vanished", "obsolete"):
                continue
            forms = [f.text or "" for f in translation.iter("numerusform")] or [translation.text or ""]
            if not any(forms):
                continue
            by_context[(name, source)] = forms
            by_source.setdefault(source, forms)
    return by_context, by_source


def qt_source(msgid, plural):
    """The Qt source text a Slint text was made from."""
    if plural is not None:
        # "{n} track" | "{n} tracks" was "%n track(s)"
        if plural.startswith(msgid):
            folded = msgid + "(" + plural[len(msgid):] + ")"
            return [folded.replace("{n}", "%n"), plural.replace("{n}", "%n")]
        return [plural.replace("{n}", "%n")]
    count = 0

    def number(m):
        nonlocal count
        count += 1
        return "%" + (str(int(m.group(1)) + 1) if m.group(1) else str(count))

    return [re.sub(r"\{(\d*)\}", number, msgid)]


def slint_text(text):
    """%1, %2 back to {0}, {1} (plain {} when there is only %1); %n to {n}"""
    text = text.replace("%n", "{n}")
    if re.search(r"%[2-9]", text):
        return re.sub(r"%(\d)", lambda m: "{" + str(int(m.group(1)) - 1) + "}", text)
    return text.replace("%1", "{}")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    entries = read_pot(Path(sys.argv[1]))
    for ts in sorted((ROOT / "i18n").glob("quince_*.ts")):
        lang = ts.stem.split("_", 1)[1]
        by_context, by_source = read_ts(ts)
        out = ['msgid ""', 'msgstr ""', '"Content-Type: text/plain; charset=UTF-8\\n"',
               f'"Language: {lang}\\n"', '"Plural-Forms: nplurals=2; plural=(n != 1);\\n"', ""]
        missing = 0
        for context, msgid, plural, refs in entries:
            forms = None
            for source in qt_source(msgid, plural):
                forms = by_context.get((context, source)) or by_source.get(source)
                if not forms and source in QT_TEXTS.get(lang, {}):
                    forms = [QT_TEXTS[lang][source]]
                if forms:
                    break
            if not forms:
                missing += 1
                print(f"{lang}: no translation for {context}: {msgid!r}", file=sys.stderr)
            out += refs
            if context:
                out.append("msgctxt " + po_quote(context))
            out.append("msgid " + po_quote(msgid))
            if plural is not None:
                out.append("msgid_plural " + po_quote(plural))
                for i in range(2):
                    form = forms[min(i, len(forms) - 1)] if forms else ""
                    out.append(f"msgstr[{i}] " + po_quote(slint_text(form)))
            else:
                out.append("msgstr " + po_quote(slint_text(forms[0]) if forms else ""))
            out.append("")
        target = ROOT / "i18n" / "slint" / lang / "LC_MESSAGES" / "quince-slint.po"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("\n".join(out), encoding="utf-8")
        print(f"{lang}: {len(entries) - missing} of {len(entries)} texts translated -> {target.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
