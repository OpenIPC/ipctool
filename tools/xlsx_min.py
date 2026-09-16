#!/usr/bin/env python3
"""Read an .xlsx into {sheet name: [ {column letter: text}, ... ]}.

Enough of the format to read a HiSilicon PINOUT workbook and nothing more:
shared strings, inline strings, and cell text. No formulas, no styles, no
dates, no writing. openpyxl would do this too, but it is a dependency CI
would have to install for one offline maintenance tool, and the subset that
matters here is thirty lines.

Cells are keyed by column letter rather than index so a sheet that gains a
column on the left does not silently shift every lookup.
"""

import re
import zipfile
from xml.etree import ElementTree as ET

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
RNS = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"
COL = re.compile(r"[A-Z]+")


def _shared_strings(z):
    try:
        root = ET.fromstring(z.read("xl/sharedStrings.xml"))
    except KeyError:
        return []
    return ["".join(t.text or "" for t in si.iter(NS + "t"))
            for si in root.findall(NS + "si")]


def sheets(path):
    z = zipfile.ZipFile(path)
    shared = _shared_strings(z)

    targets = {}
    for rel in ET.fromstring(z.read("xl/_rels/workbook.xml.rels")):
        targets[rel.get("Id")] = rel.get("Target").lstrip("/")

    out = {}
    for sheet in ET.fromstring(z.read("xl/workbook.xml")).iter(NS + "sheet"):
        target = targets[sheet.get(RNS + "id")]
        if not target.startswith("xl/"):
            target = "xl/" + target
        rows = []
        for row in ET.fromstring(z.read(target)).iter(NS + "row"):
            cells = {}
            for c in row.findall(NS + "c"):
                col = COL.match(c.get("r") or "")
                if col is None:
                    continue
                if c.get("t") == "inlineStr":
                    text = "".join(t.text or "" for t in c.iter(NS + "t"))
                else:
                    v = c.find(NS + "v")
                    if v is None or v.text is None:
                        continue
                    text = (shared[int(v.text)] if c.get("t") == "s"
                            else v.text)
                cells[col.group(0)] = text.strip()
            rows.append(cells)
        out[sheet.get("name")] = rows
    return out
