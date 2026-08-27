#!/usr/bin/env python3
"""Convert a gettext PO file into the compact JSON catalog the LSP loads.

Output shape:
    {"Save": "Speichern", "%s stream": ["%s Stream", "%s Streams"]}

Singular-only entries map to a plain string, plural entries to an array of
msgstr forms. Untranslated and fuzzy entries are dropped entirely so the
browser falls back to the English msgid.

Usage: po2json.py <input.po> <output.json>
"""

import json
import re
import sys

# "..." possibly containing escapes
_STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
_ESCAPES = {
    "n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "a": "\a",
    "b": "\b", "f": "\f", "v": "\v",
}


def unescape(text):
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            if nxt in _ESCAPES:
                out.append(_ESCAPES[nxt])
                i += 2
                continue
            if nxt == "x":
                m = re.match(r"\\x([0-9a-fA-F]{1,2})", text[i:])
                if m:
                    out.append(chr(int(m.group(1), 16)))
                    i += len(m.group(0))
                    continue
            m = re.match(r"\\([0-7]{1,3})", text[i:])
            if m:
                out.append(chr(int(m.group(1), 8)))
                i += len(m.group(0))
                continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_po(path):
    """Yield dicts with keys: msgid, msgid_plural, msgstr (list), fuzzy."""
    def blank():
        return {"msgid": None, "msgid_plural": None, "msgstr": {},
                "fuzzy": False, "ctxt": None}

    entry = blank()
    target = None  # where continuation lines append: ("msgid",) or ("msgstr", n)

    def flush():
        nonlocal entry, target
        yield_me = entry if entry["msgid"] is not None else None
        entry = blank()
        target = None
        return yield_me

    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                done = flush()
                if done:
                    yield done
                continue
            if line.startswith("#"):
                if line.startswith("#,") and "fuzzy" in line:
                    entry["fuzzy"] = True
                continue
            if line.startswith("msgctxt"):
                # The LSP catalog is keyed on the bare msgid, so contextual
                # entries have nowhere to go; record it so we can drop them.
                entry["ctxt"] = unescape(_join(line))
                target = ("ctxt",)
                continue
            if line.startswith("msgid_plural"):
                entry["msgid_plural"] = unescape(_join(line))
                target = ("msgid_plural",)
                continue
            if line.startswith("msgid"):
                entry["msgid"] = unescape(_join(line))
                target = ("msgid",)
                continue
            if line.startswith("msgstr["):
                idx = int(line[7:line.index("]")])
                entry["msgstr"][idx] = unescape(_join(line))
                target = ("msgstr", idx)
                continue
            if line.startswith("msgstr"):
                entry["msgstr"][0] = unescape(_join(line))
                target = ("msgstr", 0)
                continue
            if line.startswith('"') and target:
                chunk = unescape(_join(line))
                if target[0] == "ctxt":
                    entry["ctxt"] += chunk
                elif target[0] == "msgid":
                    entry["msgid"] += chunk
                elif target[0] == "msgid_plural":
                    entry["msgid_plural"] += chunk
                elif target[0] == "msgstr":
                    entry["msgstr"][target[1]] += chunk
                continue
    done = flush()
    if done:
        yield done


def _join(line):
    """Concatenate every quoted string on a PO line."""
    return "".join(m.group(1) for m in _STRING.finditer(line))


def main(argv):
    if len(argv) != 3:
        if __doc__:
            sys.stderr.write(__doc__)
        return 1
    catalog = {}
    for entry in parse_po(argv[1]):
        msgid = entry["msgid"]
        if msgid == "" or entry["fuzzy"] or entry["ctxt"] is not None:
            continue  # PO header / unreviewed translation / unsupported context
        if entry["msgid_plural"] is not None:
            forms = [entry["msgstr"].get(i, "") for i in range(max(entry["msgstr"]) + 1)] \
                if entry["msgstr"] else []
            if any(forms):
                catalog[msgid] = forms
        else:
            msgstr = entry["msgstr"].get(0, "")
            if msgstr:
                catalog[msgid] = msgstr
    with open(argv[2], "w", encoding="utf-8") as out:
        json.dump(catalog, out, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
