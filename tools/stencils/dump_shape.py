#!/usr/bin/env -S python3 -I
"""Prints one decoded set or shape of resources/stencils/stencils.bin as XML.

Usage: python3 -I tools/stencils/dump_shape.py <set path, e.g. flowchart.xml> [shape index]
       [--pack resources/stencils/stencils.bin]
Used to generate test fixtures and to inspect stencils.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stencil_pack  # noqa: E402


def read_pack(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"QDSTPK01"
    (count,) = struct.unpack(">I", data[8:12])
    pos = 12
    for _ in range(count):
        (n,) = struct.unpack(">H", data[pos:pos + 2])
        name = data[pos + 2:pos + 2 + n].decode("utf-8")
        pos += 2 + n
        (m,) = struct.unpack(">I", data[pos:pos + 4])
        yield name, data[pos + 4:pos + 4 + m]
        pos += 4 + m


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("set")
    parser.add_argument("shape", nargs="?", type=int)
    parser.add_argument("--pack", default="resources/stencils/stencils.bin")
    args = parser.parse_args(argv)
    for name, blob in read_pack(args.pack):
        if name == args.set:
            root = stencil_pack.decode_inflated(stencil_pack.inflate_blob(blob))
            node = root if args.shape is None else root.children[args.shape]
            sys.stdout.write(stencil_pack.to_xml(node))
            return
    sys.exit("set not found: " + args.set)


if __name__ == "__main__":
    main(sys.argv[1:])
