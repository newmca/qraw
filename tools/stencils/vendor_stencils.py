#!/usr/bin/env -S python3 -I
"""Vendors draw.io's stencil sets into resources/stencils (D-022).

Usage: python3 -I tools/stencils/vendor_stencils.py <webapp dir> [--out resources/stencils]

Reads <webapp>/js/stencils.min.js as text (never executed) and writes
stencils.bin (format: docs/design/render.md section 7), index.tsv and a
byte-exact copy of the upstream stencils/LICENSE. README.md is maintained by
hand. The output is deterministic.
"""

import argparse
import base64
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stencil_pack  # noqa: E402

SOURCE_COMMENT = "# source jgraph/drawio c72c9a98 js/stencils.min.js"
ENTRY = re.compile(r"^f\['([^']+)'\] = '([^']*)';", re.M)


def stencil_key(set_name, shape_name):
    """lower(shapes@name) + '.' + lower(shape@name with ' ' -> '_')"""
    return set_name.lower() + "." + shape_name.lower().replace(" ", "_")


def read_entries(webapp):
    path = os.path.join(webapp, "js", "stencils.min.js")
    with open(path, encoding="utf-8") as f:
        source = f.read()
    entries = {}
    for m in ENTRY.finditer(source):
        entries[m.group(1)] = base64.b64decode(m.group(2))
    return dict(sorted(entries.items()))


def write_pack(entries, out_path):
    with open(out_path, "wb") as f:
        f.write(b"QDSTPK01")
        f.write(struct.pack(">I", len(entries)))
        for path, blob in entries.items():
            raw = path.encode("utf-8")
            f.write(struct.pack(">H", len(raw)) + raw)
            f.write(struct.pack(">I", len(blob)) + blob)


def build_index(entries):
    """Returns ({key: path}, duplicate count); the first path in sort order wins."""
    index = {}
    duplicates = 0
    for path, blob in entries.items():
        root = stencil_pack.decode_inflated(stencil_pack.inflate_blob(blob))
        set_name = root.attrs.get("name", "")
        for shape in root.children:
            if shape.name != "shape":
                continue
            key = stencil_key(set_name, shape.attrs.get("name", ""))
            if key in index:
                duplicates += 1
            else:
                index[key] = path
    return index, duplicates


def write_index(index, out_path):
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(SOURCE_COMMENT + "\n")
        for key in sorted(index):
            f.write("%s\t%s\n" % (key, index[key]))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("webapp", help="upstream src/main/webapp directory")
    parser.add_argument("--out", default="resources/stencils")
    args = parser.parse_args(argv)

    entries = read_entries(args.webapp)
    if not entries:
        parser.error("no stencil entries found in js/stencils.min.js")
    os.makedirs(args.out, exist_ok=True)
    write_pack(entries, os.path.join(args.out, "stencils.bin"))
    index, duplicates = build_index(entries)
    write_index(index, os.path.join(args.out, "index.tsv"))
    shutil.copyfile(os.path.join(args.webapp, "stencils", "LICENSE"), os.path.join(args.out, "LICENSE"))
    print("%d sets, %d keys" % (len(entries), len(index)))
    print("duplicate keys: %d" % duplicates, file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
