"""Decoder for draw.io's packed stencil sets (stdlib only).

Port of decode() in upstream js/stencils.min.js (jgraph/drawio c72c9a98) as
documented in docs/research/drawio-rendering-data.md section 1. The C++
counterpart is decodePackedStencilSet() in src/render/stencil_pack.cpp.
"""

import struct
import zlib

DRAW_ELEMENTS = frozenset(
    "move line quad curve arc rect roundrect ellipse text image include-shape".split())
X_ATTRS = frozenset(("x", "x1", "x2", "x3"))
Y_ATTRS = frozenset(("y", "y1", "y2", "y3"))


class Node:
    __slots__ = ("name", "attrs", "children")

    def __init__(self, name):
        self.name = name
        self.attrs = {}      # insertion ordered
        self.children = []


def format_number(n):
    """String(n / 1000) for an integer n (thousandths)."""
    if n % 1000 == 0:
        return str(n // 1000)
    return repr(n / 1000).rstrip("0").rstrip(".")


def inflate_blob(blob):
    return zlib.decompress(blob, -15)


def decode_inflated(d):
    """Decodes the raw-inflated bytes of one set; returns the root <shapes> Node."""
    lengths = struct.unpack(">5I", d[:20])
    streams = []
    offset = 20
    for n in lengths:
        streams.append(d[offset:offset + n])
        offset += n
    ops = streams[0]
    strings = streams[4].decode("utf-8").split("\0")
    pos = [0, 0, 0, 0]
    string_index = 0
    names = []
    attr_names = []
    root = Node(None)
    stack = [root]
    px = py = 0
    sums = {}

    def read(k):
        n = 0
        m = 1
        while True:
            c = streams[k][pos[k]]
            pos[k] += 1
            n += (c & 127) * m
            m *= 128
            if not c & 128:
                break
        return -(n + 1) // 2 if n % 2 else n // 2

    p = 0
    while p < len(ops):
        op = ops[p]
        p += 1
        if op == 255:
            stack.pop()
            continue
        if op >> 1 == len(names):
            names.append(strings[string_index])
            string_index += 1
        name = names[op >> 1]
        count = ops[p]
        p += 1
        node = Node(name)
        stack[-1].children.append(node)
        if name == "shape":
            px = py = 0
            sums = {}
        for _ in range(count):
            attr_id = ops[p]
            p += 1
            if attr_id == len(attr_names):
                attr_names.append(strings[string_index])
                string_index += 1
            attr = attr_names[attr_id]
            type_byte = ops[p]
            p += 1
            if type_byte == 0:
                node.attrs[attr] = strings[string_index]
                string_index += 1
            elif name in DRAW_ELEMENTS and attr in X_ATTRS:
                px += read(1)
                node.attrs[attr] = format_number(px)
            elif name in DRAW_ELEMENTS and attr in Y_ATTRS:
                py += read(2)
                node.attrs[attr] = format_number(py)
            else:
                key = name + " " + attr
                sums[key] = sums.get(key, 0) + read(3)
                node.attrs[attr] = format_number(sums[key])
        if not op & 1:
            stack.append(node)
    return root.children[0]


def to_xml(node, indent=0):
    from xml.sax.saxutils import quoteattr
    pad = " " * indent
    attrs = "".join(" %s=%s" % (k, quoteattr(v)) for k, v in node.attrs.items())
    if not node.children:
        return "%s<%s%s/>\n" % (pad, node.name, attrs)
    inner = "".join(to_xml(c, indent + 1) for c in node.children)
    return "%s<%s%s>\n%s%s</%s>\n" % (pad, node.name, attrs, inner, pad, node.name)
