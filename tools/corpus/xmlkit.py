"""Tiny deterministic builder for draw.io (.drawio) XML used by the corpus generator.

Everything here is string based on purpose: the generator must be able to emit
odd-but-valid documents (single quotes, unknown elements, comments, ...) that a
real DOM serializer would normalize away.

Page compression matches draw.io:
    base64(deflateRaw(encodeURIComponent(xml)))
"""

from __future__ import annotations

import base64
import random
import struct
import zlib
from urllib.parse import quote, unquote

# --------------------------------------------------------------------------- codec

_URI_SAFE = "~()*!.'"  # characters JS encodeURIComponent leaves alone (besides alnum -_.)


def deflate_raw(data: bytes) -> bytes:
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def inflate_raw(data: bytes) -> bytes:
    return zlib.decompress(data, -15)


def compress_page(xml: str, legacy: bool = False) -> str:
    """Encode a page like draw.io. `legacy=True` skips the URI encoding step
    (very old draw.io/mxGraph versions wrote the deflated raw XML directly)."""
    text = xml if legacy else quote(xml, safe=_URI_SAFE)
    return base64.b64encode(deflate_raw(text.encode("utf-8"))).decode("ascii")


def decompress_page(text: str) -> tuple[str, str]:
    """Return (xml, kind) where kind is 'deflate' (URI-encoded) or 'legacy'."""
    raw = inflate_raw(base64.b64decode("".join(text.split())))
    s = raw.decode("utf-8")
    if s.lstrip().startswith("<"):
        return s, "legacy"
    return unquote(s), "deflate"


def wrap_lines(s: str, width: int = 76) -> str:
    return "\n".join(s[i:i + width] for i in range(0, len(s), width))


def png_bytes(w: int, h: int, rgb: tuple[int, int, int]) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def png_data_uri(w: int = 2, h: int = 2, rgb=(200, 40, 40)) -> str:
    """Style-safe data URI (draw.io drops ';base64' because ';' separates style tokens)."""
    return "data:image/png," + base64.b64encode(png_bytes(w, h, rgb)).decode("ascii")


# --------------------------------------------------------------------------- escaping

def esc_attr(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;").replace("\n", "&#10;").replace("\r", "&#13;").replace("\t", "&#9;"))


def esc_text(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def num(v) -> str:
    if isinstance(v, str):
        return v
    if isinstance(v, float):
        if v == int(v) and abs(v) < 1e15:
            return str(int(v))
        return repr(round(v, 4))
    return str(v)


def attrs_str(d: dict | None) -> str:
    if not d:
        return ""
    return " ".join(f'{k}="{esc_attr(num(v))}"' for k, v in d.items() if v is not None)


def el(tag: str, attrib: dict | None = None, children=(), text: str | None = None, nl: bool = False) -> str:
    """Element builder. `text` must already be escaped; `children` are XML strings."""
    a = attrs_str(attrib)
    head = f"<{tag}" + (f" {a}" if a else "")
    children = list(children)
    if not children and text is None:
        return head + "/>"
    if nl:
        inner = "".join("\n" + c for c in children) + "\n"
    else:
        inner = "".join(children)
    return f"{head}>{text or ''}{inner}</{tag}>"


def pt(x, y, as_: str | None = None) -> str:
    return el("mxPoint", {"x": x, "y": y, "as": as_})


def geo(x=None, y=None, w=None, h=None, children=(), **extra) -> str:
    a: dict = {}
    for k, v in (("x", x), ("y", y), ("width", w), ("height", h)):
        if v is not None:
            a[k] = v
    a.update(extra)
    a["as"] = "geometry"
    return el("mxGeometry", a, children)


# --------------------------------------------------------------------------- document model

MODEL_DEFAULT = {
    "dx": 1422, "dy": 794, "grid": 1, "gridSize": 10, "guides": 1, "tooltips": 1, "connect": 1,
    "arrows": 1, "fold": 1, "page": 1, "pageScale": 1, "pageWidth": 827, "pageHeight": 1169,
    "math": 0, "shadow": 0,
}

MXFILE_WEB = {
    "host": "app.diagrams.net",
    "agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "version": "26.0.9", "etag": "qT3vB8nQ2k1xYzWmL0aP", "type": "device",
}
MXFILE_DESKTOP = {
    "host": "Electron", "modified": "2024-08-01T10:00:00.000Z",
    "agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) draw.io/24.7.5 Chrome/126.0.6478.183 Electron/31.3.0 Safari/537.36",
    "etag": "x9Kf2LmPq7", "version": "24.7.5", "type": "device", "compressed": "false",
}
MXFILE_OLD = {"host": "localhost", "modified": "2019-07-12T12:49:27.384Z", "agent": "5.0 (X11)",
              "etag": "AbC123", "version": "10.9.5", "type": "device", "compressed": "true"}

_ID_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"


class Page:
    """One diagram page. Cells are appended in call order; the page can be
    serialized as an inline model or in one of the compressed encodings."""

    def __init__(self, name: str = "Page-1", *, pid: str | None = None, enc: str = "plain",
                 seed: int = 0, ids: str = "seq", pretty: bool = False,
                 model: dict | None = None, layer: str = "1", diag_attrs: dict | None = None):
        self.name = name
        self.rng = random.Random(seed)
        self.enc = enc  # plain | deflate | legacy | wrapped | empty
        self.ids = ids  # seq | rand
        self.pretty = pretty
        self.model = dict(MODEL_DEFAULT) if model is None else dict(model)
        self.layer_id = layer
        self.layer_attrs: dict = {}
        self.zero_attrs: dict = {}
        self.diag_attrs = diag_attrs or {}
        self.layers: list[str] = []
        self.cells: list[str] = []
        self.root_head: list[str] = []   # unknown nodes before cell 0
        self.root_tail: list[str] = []   # unknown nodes after the last cell
        self.model_pre: list[str] = []   # unknown nodes inside mxGraphModel before <root>
        self.model_post: list[str] = []  # ... after </root>
        self.diag_post: list[str] = []   # unknown nodes inside <diagram> after the inline model
        self._n = 1
        self.pid = pid or self.rand_id()

    # ---- ids
    def rand_id(self, n: int = 20) -> str:
        return "".join(self.rng.choice(_ID_ALPHABET) for _ in range(n))

    def nid(self) -> str:
        self._n += 1
        return str(self._n) if self.ids == "seq" else self.rand_id()

    @property
    def cell_count(self) -> int:
        return len(self.cells)

    # ---- layers
    def add_layer(self, name: str, id: str | None = None, visible: bool = True, locked: bool = False,
                  style: str | None = None, **extra) -> str:
        lid = id or self.nid()
        a: dict = {"id": lid, "value": name, "style": style if style is not None else ("locked=1;" if locked else None),
                   "parent": "0"}
        if not visible:
            a["visible"] = "0"
        a.update(extra)
        self.layers.append(el("mxCell", a))
        return lid

    # ---- vertices
    def v(self, value: str, x, y, w, h, style: str | None = None, *, parent: str | None = None,
          id: str | None = None, obj: tuple[str, dict] | None = None, cell_attrs: dict | None = None,
          geo_children=(), geo_attrs: dict | None = None, cell_children=(), label_key: str = "label") -> str:
        """Add a vertex. `obj=("object", {...custom attrs})` wraps it in <object>/<UserObject>."""
        cid = id or self.nid()
        parent = parent or self.layer_id
        g = geo(x, y, w, h, geo_children, **(geo_attrs or {}))
        cell = {"style": style, "vertex": "1", "parent": parent}
        cell.update(cell_attrs or {})
        if obj is None:
            self.cells.append(el("mxCell", {"id": cid, "value": value, **cell}, [g, *cell_children]))
        else:
            tag, custom = obj
            wrapper = {label_key: value, **custom, "id": cid}
            self.cells.append(el(tag, wrapper, [el("mxCell", cell, [g, *cell_children])]))
        return cid

    # ---- edges
    def e(self, source: str | None, target: str | None, value: str = "", style: str | None = None, *,
          points=None, sp=None, tp=None, offset=None, label_pos=None, parent: str | None = None,
          id: str | None = None, obj: tuple[str, dict] | None = None, cell_attrs: dict | None = None,
          geo_attrs: dict | None = None, geo_extra=(), cell_children=()) -> str:
        cid = id or self.nid()
        parent = parent or self.layer_id
        kids = []
        if sp is not None:
            kids.append(pt(sp[0], sp[1], "sourcePoint"))
        if tp is not None:
            kids.append(pt(tp[0], tp[1], "targetPoint"))
        if points:
            kids.append(el("Array", {"as": "points"}, [pt(x, y) for x, y in points]))
        if offset is not None:
            kids.append(pt(offset[0], offset[1], "offset"))
        kids.extend(geo_extra)
        lx, ly = label_pos if label_pos is not None else (None, None)
        g = geo(lx, ly, None, None, kids, **{**({"width": 50} if label_pos is None else {}), "relative": "1",
                                              **(geo_attrs or {})})
        cell = {"style": style, "edge": "1", "parent": parent, "source": source, "target": target}
        cell.update(cell_attrs or {})
        if obj is None:
            self.cells.append(el("mxCell", {"id": cid, "value": value, **cell}, [g, *cell_children]))
        else:
            tag, custom = obj
            self.cells.append(el(tag, {"label": value, **custom, "id": cid}, [el("mxCell", cell, [g, *cell_children])]))
        return cid

    def edge_label(self, edge_id: str, value: str, x: float = 0, y: float = 0, offset=(0, 0)) -> str:
        """A text cell parented to an edge (draw.io 'edgeLabel')."""
        cid = self.nid()
        g = geo(x, y, None, None, [pt(offset[0], offset[1], "offset")], relative="1")
        self.cells.append(el("mxCell", {
            "id": cid, "value": value, "parent": edge_id, "vertex": "1", "connectable": "0",
            "style": "edgeLabel;html=1;align=center;verticalAlign=middle;resizable=0;points=[];"}, [g]))
        return cid

    def raw(self, xml: str) -> None:
        self.cells.append(xml)

    # ---- serialization
    def model_xml(self) -> str:
        zero = el("mxCell", {"id": "0", **self.zero_attrs})
        lay = el("mxCell", {"id": self.layer_id, "parent": "0", **self.layer_attrs})
        root = el("root", None, [*self.root_head, zero, lay, *self.layers, *self.cells, *self.root_tail], nl=self.pretty)
        return el("mxGraphModel", self.model, [*self.model_pre, root, *self.model_post], nl=self.pretty)

    def diagram_xml(self) -> str:
        attrs = {"name": self.name, "id": self.pid, **self.diag_attrs}
        if self.enc == "empty":
            return el("diagram", attrs)
        if self.enc == "plain":
            return el("diagram", attrs, [self.model_xml(), *self.diag_post], nl=self.pretty)
        xml = self.model_xml()
        if self.enc == "deflate":
            return el("diagram", attrs, text=compress_page(xml))
        if self.enc == "legacy":
            return el("diagram", attrs, text=compress_page(xml, legacy=True))
        if self.enc == "wrapped":
            return el("diagram", attrs, text="\n" + wrap_lines(compress_page(xml)) + "\n")
        raise ValueError(self.enc)


def mxfile(pages: list[Page], attrs: dict | None = None, *, decl: bool = False, nl: bool = False,
           head: str = "", tail: str = "", extra_pre=(), extra_post=()) -> str:
    """Serialize an <mxfile>. `head`/`tail` go before/after the root element."""
    body = [*extra_pre, *[p.diagram_xml() for p in pages], *extra_post]
    text = el("mxfile", dict(MXFILE_WEB) if attrs is None else attrs, body, nl=nl)
    out = ('<?xml version="1.0" encoding="UTF-8"?>\n' if decl else "") + head + text + tail
    return out + ("\n" if nl else "")


def bare_model(p: Page) -> str:
    return p.model_xml()
