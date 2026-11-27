"""Host objects that let jsmini run draw.io's sidebar scripts as a recorder.

The scripts call `this.createVertexTemplateEntry(...)`, `this.addPaletteFunctions(...)`,
`new mxCell(...)` and friends. Here those are Python objects that *record*
what the scripts build: palettes with entries (style/size/title/tags, or a
cell tree). Nothing here renders anything or talks to a browser.

Upstream behaviour ported (js/grapheditor/Sidebar.js, c72c9a98):
  createVertexTemplateEntry :6950   createVertexTemplate :6968
  createEdgeTemplateEntry   :7025   createEdgeTemplate   :7039
  addDataEntry :996   addEntry :2121   filterTags :2688   cloneCell :2716
  addPaletteFunctions :7064   addStencilPalette :7448   getTagsForStencil :7426
"""

import base64
import math
import os
import re
import urllib.parse
import zlib

import jsmini
from jsmini import HostObject, NativeFn, UNDEF, Unsupported, to_num, to_str, truthy

# --------------------------------------------------------------------------
# Small globals
# --------------------------------------------------------------------------


def js_round(x):
    return float(math.floor(x + 0.5))


def parse_constants(source):
    """mxConstants.js: `NAME: 'string'` / `NAME: 12` members -> {name: value}."""
    consts = {}
    for m in re.finditer(r"^\s*([A-Z_0-9]+):\s*('(?:[^'\\]|\\.)*'|-?[\d.]+)\s*,?\s*$", source, re.M):
        v = m.group(2)
        consts[m.group(1)] = jsmini.unescape_js_string(v[1:-1]) if v[0] == "'" else float(v)
    return consts


def parse_resources(source):
    """resources/dia.txt: `key=English text` lines."""
    res = {}
    for line in source.splitlines():
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        res[key.strip()] = value.strip()
    return res


def make_math():
    def unary(fn):
        return NativeFn(lambda this, a: fn(to_num(a[0])))

    def extremum(fn, empty):
        def run(this, args):
            nums = [to_num(x) for x in args]
            if any(n != n for n in nums):
                return math.nan
            return fn(nums) if nums else empty
        return NativeFn(run)

    return {
        "round": unary(js_round), "floor": unary(lambda x: float(math.floor(x))),
        "ceil": unary(lambda x: float(math.ceil(x))), "abs": unary(abs), "sqrt": unary(math.sqrt),
        "sin": unary(math.sin), "cos": unary(math.cos), "tan": unary(math.tan),
        "max": extremum(max, -math.inf), "min": extremum(min, math.inf),
        "pow": NativeFn(lambda this, a: math.pow(to_num(a[0]), to_num(a[1]))),
        "PI": math.pi,
    }


def js_parse_float(this, args):
    m = re.match(r"\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?", to_str(args[0]) if args else "")
    return float(m.group().strip()) if m else math.nan


def js_parse_int(this, args):
    m = re.match(r"\s*[+-]?\d+", to_str(args[0]) if args else "")
    return float(int(m.group())) if m else math.nan


def set_style(style, key, value):
    """mxUtils.setStyle (mxgraph/src/util/mxUtils.js:4867)."""
    is_value = value is not None and value is not UNDEF and (not isinstance(value, str) or len(value) > 0)
    if style is not None and style is not UNDEF and not isinstance(style, str):
        style = to_str(style)
    if style is None or style is UNDEF or len(style) == 0:
        return "%s=%s;" % (key, to_str(value)) if is_value else (style if isinstance(style, str) else "")
    if style.startswith(key + "="):
        nxt = style.find(";")
        if is_value:
            return key + "=" + to_str(value) + (";" if nxt < 0 else style[nxt:])
        while nxt >= 0 and nxt + 1 < len(style) and style[nxt + 1] == ";":
            nxt += 1
        return "" if nxt < 0 or nxt == len(style) - 1 else style[nxt + 1:]
    index = style.find(";" + key + "=")
    if index < 0:
        if is_value:
            return style + ("" if style.endswith(";") else ";") + key + "=" + to_str(value) + ";"
        return style
    nxt = style.find(";", index + 1)
    if is_value:
        return style[:index + 1] + key + "=" + to_str(value) + (";" if nxt < 0 else style[nxt:])
    return style[:index] + (";" if nxt < 0 else style[nxt:])


class Namespace(HostObject):
    """A bag of native functions/constants, e.g. `mxUtils`, `Math`."""

    def __init__(self, members):
        self.members = members

    def js_get(self, name):
        return self.members.get(name, UNDEF)

    def js_set(self, name, value):
        self.members[name] = value


class StringFn(HostObject):
    """The global `String`: String(x) and String.fromCharCode(...)."""

    def js_call(self, args):
        return to_str(args[0]) if args else ""

    def js_get(self, name):
        if name == "fromCharCode":
            return NativeFn(lambda this, a: "".join(chr(int(to_num(x))) for x in a))
        return UNDEF


def make_mxutils():
    def bind(this, args):
        target, fn = args[0], args[1]
        extra = list(args[2:])
        return NativeFn(lambda t, a: fn.call(target, extra + list(a)))

    def get_value(this, args):
        arr, key = args[0], args[1]
        default = args[2] if len(args) > 2 else UNDEF
        value = arr.get(key, None) if isinstance(arr, dict) else None
        return default if value is None or value is UNDEF else value

    def index_of(this, args):
        for i, x in enumerate(args[0] or []):
            if jsmini.strict_equals(x, args[1]):
                return float(i)
        return -1.0

    return Namespace({
        "bind": NativeFn(bind),
        "setStyle": NativeFn(lambda this, a: set_style(a[0], to_str(a[1]), a[2] if len(a) > 2 else UNDEF)),
        "getValue": NativeFn(get_value),
        "indexOf": NativeFn(index_of),
        "contains": NativeFn(lambda this, a: index_of(this, a) >= 0),
        "clone": NativeFn(lambda this, a: dict(a[0]) if isinstance(a[0], dict) else a[0]),
    })


# --------------------------------------------------------------------------
# Model objects: mxCell, mxGeometry, mxPoint, mxRectangle
# --------------------------------------------------------------------------

class Ctor(HostObject):
    def __init__(self, factory, statics=None):
        self.factory = factory
        self.statics = statics or {}

    def js_new(self, args):
        return self.factory(args)

    def js_get(self, name):
        return self.statics.get(name, UNDEF)


class Point(HostObject):
    def __init__(self, x=0.0, y=0.0):
        self.fields = {"x": x, "y": y}

    def js_get(self, name):
        return self.fields.get(name, UNDEF)

    def js_set(self, name, value):
        self.fields[name] = value

    def clone(self):
        return Point(self.fields["x"], self.fields["y"])


def _num_arg(args, i, default=0.0):
    return to_num(args[i]) if i < len(args) and args[i] is not None and args[i] is not UNDEF else default


class Geometry(HostObject):
    def __init__(self, x=0.0, y=0.0, width=0.0, height=0.0):
        self.fields = {"x": x, "y": y, "width": width, "height": height, "relative": False,
                       "offset": None, "points": None, "sourcePoint": None, "targetPoint": None,
                       "alternateBounds": None}

    def js_get(self, name):
        if name in self.fields:
            return self.fields[name]
        method = getattr(self, "m_" + name, None)
        return NativeFn(lambda this, args, _m=method: _m(args)) if method else UNDEF

    def js_set(self, name, value):
        self.fields[name] = value

    def m_setTerminalPoint(self, args):
        self.fields["sourcePoint" if truthy(args[1]) else "targetPoint"] = args[0]
        return args[0]

    def m_clone(self, args):
        return self.clone()

    def clone(self):
        g = Geometry()
        g.fields = dict(self.fields)
        for key in ("offset", "sourcePoint", "targetPoint"):
            if isinstance(g.fields[key], Point):
                g.fields[key] = g.fields[key].clone()
        if isinstance(g.fields["points"], list):
            g.fields["points"] = [p.clone() if isinstance(p, Point) else p for p in g.fields["points"]]
        return g


class Cell(HostObject):
    def __init__(self, value="", geometry=None, style=None):
        self.fields = {"value": value, "geometry": geometry, "style": style, "vertex": False, "edge": False,
                       "connectable": True, "visible": True, "collapsed": False, "parent": None,
                       "children": None, "source": None, "target": None, "id": None, "attrs": None}

    def js_get(self, name):
        if name in self.fields:
            return self.fields[name]
        method = getattr(self, "m_" + name, None)
        return NativeFn(lambda this, args, _m=method: _m(args)) if method else UNDEF

    def js_set(self, name, value):
        self.fields[name] = value

    # mxCell methods used by the sidebar scripts
    def m_insert(self, args):
        child = args[0]
        if isinstance(child, Cell):
            if child.fields["parent"] is not None and child.fields["parent"] is not self:
                siblings = child.fields["parent"].fields["children"] or []
                if child in siblings:
                    siblings.remove(child)
            child.fields["parent"] = self
            if self.fields["children"] is None:
                self.fields["children"] = []
            if child not in self.fields["children"]:
                if len(args) > 1 and args[1] is not None and args[1] is not UNDEF:
                    self.fields["children"].insert(int(to_num(args[1])), child)
                else:
                    self.fields["children"].append(child)
        return child

    def m_clone(self, args):
        # mxCell.clone: mxTransient = id, value, parent, source, target, children, edges.
        c = Cell(self.fields["value"], self.fields["geometry"].clone() if isinstance(self.fields["geometry"], Geometry)
                 else self.fields["geometry"], self.fields["style"])
        for key in ("vertex", "edge", "connectable", "visible", "collapsed"):
            c.fields[key] = self.fields[key]
        return c

    def m_insertEdge(self, args):
        # mxCell.insertEdge(edge, isOutgoing): the edge's terminal becomes this cell.
        args[0].fields["source" if truthy(args[1]) else "target"] = self
        return args[0]

    def m_setConnectable(self, args):
        self.fields["connectable"] = truthy(args[0])

    def m_setValue(self, args):
        self.fields["value"] = args[0]

    def m_getValue(self, args):
        return self.fields["value"]

    def m_setStyle(self, args):
        self.fields["style"] = args[0]

    def m_getStyle(self, args):
        return self.fields["style"]

    def m_setGeometry(self, args):
        self.fields["geometry"] = args[0]

    def m_getGeometry(self, args):
        return self.fields["geometry"]

    def m_setVertex(self, args):
        self.fields["vertex"] = truthy(args[0])

    def m_setEdge(self, args):
        self.fields["edge"] = truthy(args[0])

    def m_setVisible(self, args):
        self.fields["visible"] = truthy(args[0])

    def m_setCollapsed(self, args):
        self.fields["collapsed"] = truthy(args[0])

    def m_getChildCount(self, args):
        return float(len(self.fields["children"] or []))

    def m_getChildAt(self, args):
        children = self.fields["children"] or []
        i = int(to_num(args[0]))
        return children[i] if 0 <= i < len(children) else None

    def m_getParent(self, args):
        return self.fields["parent"]


class ModelHost(HostObject):
    """The few `graph.model` calls the builders make."""

    def m_setTerminal(self, args):
        args[0].fields["source" if truthy(args[2]) else "target"] = args[1]
        return args[1]

    def m_setGeometry(self, args):
        args[0].fields["geometry"] = args[1]

    def m_setValue(self, args):
        args[0].fields["value"] = args[1]

    def m_setStyle(self, args):
        args[0].fields["style"] = args[1]

    def m_getGeometry(self, args):
        return args[0].fields["geometry"]


class GraphHost(HostObject):
    """The `graph` the palette scripts consult (default settings only)."""

    def __init__(self):
        self.model = ModelHost()
        self.p_vertexFontSize = None
        self.p_defaultVertexStyle = {}
        self.p_defaultEdgeStyle = {}

    def js_prop(self, name):
        if name == "model":
            return self.model
        return super().js_prop(name)

    def m_appendFontSize(self, args):
        style = args[0] if isinstance(args[0], str) else ""
        size = args[1] if len(args) > 1 else None
        if size is not None and size is not UNDEF:
            if style and not style.endswith(";"):
                style += ";"
            style += "fontSize=%s;" % to_str(size)
        return style

    def m_getCellGeometry(self, args):
        return args[0].fields["geometry"]

    def m_setLinkForCell(self, args):
        self.m_setAttributeForCell([args[0], "link", args[1]])

    def m_setAttributeForCell(self, args):
        # Graph.setAttributeForCell: the value becomes an <object label=...> with extra attributes.
        cell = args[0]
        if cell.fields["attrs"] is None:
            cell.fields["attrs"] = {}
        cell.fields["attrs"][jsmini.to_str(args[1])] = jsmini.to_str(args[2])


# --------------------------------------------------------------------------
# Cells -> mxGraphModel XML (what Sidebar.createItem inserts)
# --------------------------------------------------------------------------

def fmt_num(v):
    v = to_num(v)
    r = round(v, 6)
    return str(int(r)) if r == int(r) else repr(r)


def xml_attr(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
            .replace("\n", "&#xa;").replace("\r", "&#xd;").replace("\t", "&#x9;"))


def _point_xml(p, as_name=None):
    attrs = ""
    if p.fields["x"] not in (0, 0.0):
        attrs += ' x="%s"' % fmt_num(p.fields["x"])
    if p.fields["y"] not in (0, 0.0):
        attrs += ' y="%s"' % fmt_num(p.fields["y"])
    return "<mxPoint%s%s/>" % (attrs, ' as="%s"' % as_name if as_name else "")


def _geometry_xml(g):
    f = g.fields
    attrs = ""
    for key in ("x", "y", "width", "height"):
        if f[key] not in (0, 0.0):
            attrs += ' %s="%s"' % (key, fmt_num(f[key]))
    if truthy(f["relative"]):
        attrs += ' relative="1"'
    inner = ""
    if isinstance(f["sourcePoint"], Point):
        inner += _point_xml(f["sourcePoint"], "sourcePoint")
    if isinstance(f["targetPoint"], Point):
        inner += _point_xml(f["targetPoint"], "targetPoint")
    if isinstance(f["points"], list) and f["points"]:
        inner += '<Array as="points">%s</Array>' % "".join(_point_xml(p) for p in f["points"] if isinstance(p, Point))
    if isinstance(f["offset"], Point):
        inner += _point_xml(f["offset"], "offset")
    if isinstance(f["alternateBounds"], Geometry):
        rect = f["alternateBounds"].fields
        ra = "".join(' %s="%s"' % (k, fmt_num(rect[k])) for k in ("x", "y", "width", "height") if rect[k] not in (0, 0.0))
        inner += '<mxRectangle%s as="alternateBounds"/>' % ra
    if inner:
        return '<mxGeometry%s as="geometry">%s</mxGeometry>' % (attrs, inner)
    return '<mxGeometry%s as="geometry"/>' % attrs


def cells_to_xml(cells):
    """Serialises cell trees like mxCodec: ids 2.., top-level parent "1"."""
    order = []

    def number(cell):
        order.append(cell)
        for child in cell.fields["children"] or []:
            number(child)

    for cell in cells:
        number(cell)
    ids = {id(c): str(i + 2) for i, c in enumerate(order)}
    out = ['<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>']
    for cell in order:
        f = cell.fields
        value = f["value"]
        if value is None or value is UNDEF:
            value = ""
        if not isinstance(value, str):
            raise Unsupported("cell value is not a string")
        attrs = ' id="%s" value="%s"' % (ids[id(cell)], xml_attr(value))
        if isinstance(f["style"], str) and f["style"]:
            attrs += ' style="%s"' % xml_attr(f["style"])
        if truthy(f["vertex"]):
            attrs += ' vertex="1"'
        if truthy(f["edge"]):
            attrs += ' edge="1"'
        if not truthy(f["connectable"]):
            attrs += ' connectable="0"'
        if not truthy(f["visible"]):
            attrs += ' visible="0"'
        if truthy(f["collapsed"]):
            attrs += ' collapsed="1"'
        parent = f["parent"]
        attrs += ' parent="%s"' % (ids[id(parent)] if parent is not None else "1")
        for key in ("source", "target"):
            terminal = f[key]
            if isinstance(terminal, Cell):
                if id(terminal) not in ids:
                    raise Unsupported("edge %s outside the template" % key)
                attrs += ' %s="%s"' % (key, ids[id(terminal)])
        geo = f["geometry"]
        cell_xml = "<mxCell%s>%s</mxCell>"
        geo_xml = _geometry_xml(geo) if isinstance(geo, Geometry) else ""
        if f["attrs"]:
            # id and label move to the <object> wrapper, as mxCodec writes a cell with an XML value
            extra = "".join(' %s="%s"' % (k, xml_attr(v)) for k, v in f["attrs"].items())
            wrapper = ' label="%s"%s id="%s"' % (xml_attr(value), extra, ids[id(cell)])
            attrs = attrs.replace(' id="%s" value="%s"' % (ids[id(cell)], xml_attr(value)), "")
            out.append("<object%s><mxCell%s>%s</mxCell></object>" % (wrapper, attrs, geo_xml))
        else:
            out.append(cell_xml % (attrs, geo_xml))
    out.append("</root></mxGraphModel>")
    return "".join(out)


def decode_data_entry(data):
    """Graph.decompress: base64 -> raw inflate -> decodeURIComponent."""
    raw = zlib.decompress(base64.b64decode(data), -15)
    return urllib.parse.unquote(raw.decode("utf-8"))


_MODEL_RE = re.compile(r"<mxGraphModel\b.*?</mxGraphModel>", re.S)


def normalise_model_xml(xml):
    """Ensures an <mxGraphModel> with layer cells 0/1 and top-level parent "1"."""
    m = _MODEL_RE.search(xml)
    if m is None:
        raise Unsupported("data entry is not an mxGraphModel")
    xml = m.group()
    root_match = re.search(r"<root>(.*)</root>", xml, re.S)
    if root_match is None:
        raise Unsupported("data entry has no <root>")
    body = root_match.group(1)
    has_layers = '<mxCell id="0"' in body and '<mxCell id="1"' in body
    if not has_layers:
        body = '<mxCell id="0"/><mxCell id="1" parent="0"/>' + body
    return "<mxGraphModel><root>%s</root></mxGraphModel>" % body


# --------------------------------------------------------------------------
# Entries and palettes
# --------------------------------------------------------------------------

class Entry(HostObject):
    """One sidebar item as the scripts create it (callable like upstream's fn)."""

    def __init__(self, kind, title, tags="", w=0.0, h=0.0, style="", value="", show_label=True,
                 cells=None, xml=None):
        self.kind = kind  # "vertex" | "edge" | "cells" | None (skipped)
        self.title = title
        self.tags = tags
        self.w = w
        self.h = h
        self.style = style
        self.value = value
        self.show_label = show_label
        self.cells = cells
        self.xml = xml

    def js_call(self, args):
        return self

    def js_get(self, name):
        return UNDEF


SKIPPED = Entry(None, "")

_TAG_SPLIT = re.compile(r"[/,()]")


def normalise_tags(tags):
    """Sidebar.addEntry: lower-case, '/,()' -> ' ', split, drop words shorter than 2."""
    if not isinstance(tags, str):
        return []
    seen = []
    for word in _TAG_SPLIT.sub(" ", tags.lower()).split(" "):
        if len(word) > 1 and word not in seen:
            seen.append(word)
    return seen


def slug(title):
    s = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    return s or "item"


class Palette:
    def __init__(self, pid, title, library):
        self.id = pid
        self.title = title
        self.library = library
        self.entries = []
        self.skips = []  # (file, line, reason)


class Content(HostObject):
    """The `content` element of Sidebar.addPalette: collects appended entries."""

    def __init__(self):
        self.entries = []

    def m_appendChild(self, args):
        if isinstance(args[0], Entry):
            self.entries.append(args[0])
        return args[0]


def _arg(args, i):
    return args[i] if i < len(args) else UNDEF


def _opt(v):
    return None if v is UNDEF else v


def _str_or_empty(v):
    return "" if v is None or v is UNDEF else to_str(v)


class SidebarHost(HostObject):
    """`this` / `sb` inside the palette scripts."""

    def __init__(self, interp, prototype, resources, stencils):
        self.interp = interp
        self.prototype = prototype  # Sidebar.prototype dict (functions defined by the scripts)
        self.resources = resources
        self.stencils = stencils  # basename -> (package name with dot, [(name, w, h)])
        self.palettes = []
        self.pending_skips = []
        self.library = ""
        self.graph = GraphHost()

    # property / method lookup: natives first, then functions the scripts defined
    def js_get(self, name):
        method = getattr(self, "m_" + name, None)
        if method is not None:
            return NativeFn(lambda this, args, _m=method: _m(args), name)
        if name in ("graph",):
            return self.graph
        if name == "editorUi":
            return Namespace({"editor": Namespace({"graph": self.graph})})
        if name == "initialDefaultVertexStyle" or name == "initialDefaultEdgeStyle":
            return {}
        if name in self.prototype:
            return self.prototype[name]
        return UNDEF

    def js_set(self, name, value):
        self.prototype[name] = value

    # recording helpers
    def note_skip(self, pos, reason):
        file, line = pos if pos else ("?", 0)
        self.pending_skips.append((os.path.basename(file), line, reason))

    def _finish_palette(self, pid, title, entries):
        palette = Palette(pid, to_str(title), self.library)
        palette.entries = [e for e in entries if isinstance(e, Entry) and e.kind is not None]
        palette.skips = self.pending_skips
        self.pending_skips = []
        self.palettes.append(palette)

    # -- no-ops (search index, tooltips, picker)
    def m_setCurrentSearchEntryLibrary(self, args):
        self.library = to_str(args[0]) if args and args[0] not in (None, UNDEF) else ""

    def m_setPickerShapes(self, args):
        pass

    # -- tags
    def m_getTagsForStencil(self, args):
        pkg, name = to_str(args[0]), to_str(args[1])
        more = _opt(_arg(args, 2))
        tags = pkg.split(".")
        tags = [tags[0]] + [t.replace("_", " ") for t in tags[1:]]
        tags.append(name.replace("_", " "))
        if more is not None:
            tags.append(to_str(more))
        return tags[1:]

    def m_filterTags(self, args):
        if args[0] is None or args[0] is UNDEF:
            return None
        seen = []
        for word in to_str(args[0]).split(" "):
            if word not in seen:
                seen.append(word)
        return " ".join(seen)

    def m_cloneCell(self, args):
        clone = args[0].m_clone([])
        if len(args) > 1 and args[1] is not None and args[1] is not UNDEF:
            clone.fields["value"] = args[1]
        return clone

    # -- entry factories
    def m_createVertexTemplateEntry(self, args):
        style, w, h, value, title, show_label, _show_title, tags = [_arg(args, i) for i in range(8)]
        tags = _opt(tags)
        title_s = None if title in (None, UNDEF) else to_str(title)
        if tags is not None and title_s is not None:
            tags = to_str(tags) + " " + title_s
        tags = tags if tags is not None and len(to_str(tags)) > 0 else (title_s.lower() if title_s is not None else "")
        return self._vertex(style, w, h, value, title_s, show_label, tags)

    def _vertex(self, style, w, h, value, title, show_label, tags):
        return Entry("vertex", title or "", tags, to_num(w), to_num(h), _str_or_empty(style),
                     _str_or_empty(value), show_label is not False)

    def m_createVertexTemplate(self, args):
        style, w, h, value, title, show_label = [_arg(args, i) for i in range(6)]
        return self._vertex(style, w, h, value, None if title in (None, UNDEF) else to_str(title), show_label, "")

    def m_createEdgeTemplateEntry(self, args):
        style, w, h, value, title, show_label, tags = [_arg(args, i) for i in range(7)]
        title_s = _str_or_empty(title)
        tags = to_str(tags) if tags not in (None, UNDEF) and len(to_str(tags)) > 0 else title_s.lower()
        return self._edge(style, w, h, value, title_s, show_label, tags)

    def _edge(self, style, w, h, value, title, show_label, tags):
        return Entry("edge", title, tags, to_num(w), to_num(h), _str_or_empty(style), _str_or_empty(value),
                     show_label is not False)

    def m_createEdgeTemplate(self, args):
        style, w, h, value, title, show_label = [_arg(args, i) for i in range(6)]
        return self._edge(style, w, h, value, _str_or_empty(title), show_label, "")

    def m_createVertexTemplateFromCells(self, args):
        cells, w, h, title, show_label = [_arg(args, i) for i in range(5)]
        return self._from_cells(cells, w, h, title, show_label)

    def m_createEdgeTemplateFromCells(self, args):
        cells, w, h, title, show_label = [_arg(args, i) for i in range(5)]
        return self._from_cells(cells, w, h, title, show_label)

    def _from_cells(self, cells, w, h, title, show_label):
        if not isinstance(cells, list) or not all(isinstance(c, Cell) for c in cells):
            raise Unsupported("template cells are not cells")
        return Entry("cells", _str_or_empty(title), "", to_num(w), to_num(h), show_label=show_label is not False,
                     cells=cells)

    def m_createVertexTemplateFromData(self, args):
        data, w, h, title = [_arg(args, i) for i in range(4)]
        return Entry("cells", _str_or_empty(title), "", to_num(w), to_num(h),
                     xml=normalise_model_xml(decode_data_entry(to_str(data))))

    # -- addEntry family: the fn builds the entry lazily upstream; we run it now.
    def m_addEntry(self, args):
        tags, fn = args[0], args[1]
        try:
            entry = fn.call(UNDEF, [])
            if not isinstance(entry, Entry):
                raise Unsupported("addEntry builder did not return a template")
        except Unsupported as exc:
            self.note_skip(exc.pos, "addEntry builder: " + exc.message)
            return SKIPPED
        entry.tags = to_str(tags) if isinstance(tags, str) else entry.tags
        return entry

    def m_addDataEntry(self, args):
        tags, w, h, title, data = [_arg(args, i) for i in range(5)]
        tags = "" if tags in (None, UNDEF) else to_str(tags)
        if title not in (None, UNDEF):
            tags += " " + to_str(title)
        try:
            entry = self.m_createVertexTemplateFromData([data, w, h, title])
        except (Unsupported, ValueError, zlib.error) as exc:
            self.note_skip(getattr(exc, "pos", None), "addDataEntry: %s" % getattr(exc, "message", exc))
            return SKIPPED
        entry.tags = tags
        return entry

    # -- palettes
    def m_addPalette(self, args):
        pid, title, _expanded, fn = [_arg(args, i) for i in range(4)]
        content = Content()
        try:
            fn.call(UNDEF, [content])
        except Unsupported as exc:
            self.note_skip(exc.pos, "palette function aborted: " + exc.message)
        self._finish_palette(to_str(pid), title, content.entries)

    def m_addPaletteFunctions(self, args):
        pid, title, _expanded, fns = [_arg(args, i) for i in range(4)]
        content = Content()
        for fn in fns if isinstance(fns, list) else []:
            try:
                value = self.interp.call_value(fn, UNDEF, [content])
            except Unsupported as exc:
                self.note_skip(exc.pos, exc.message)
                continue
            content.m_appendChild([value])
        self._finish_palette(to_str(pid), title, content.entries)

    def m_addStencilPalette(self, args):
        pid, title, stencil_file, style, ignore, _on_init, scale, tags, custom_fns = [_arg(args, i) for i in range(9)]
        scale = 1.0 if scale in (None, UNDEF) else to_num(scale)
        style = "" if style in (None, UNDEF) else to_str(style)
        ignore = ignore if isinstance(ignore, list) else []
        tag_map = tags if isinstance(tags, dict) else {}
        entries = [e for e in (custom_fns if isinstance(custom_fns, list) else []) if isinstance(e, Entry)]
        key = os.path.basename(to_str(stencil_file))
        if key not in self.stencils:
            self.note_skip(None, "addStencilPalette: stencil set %s not found" % key)
        else:
            package, shapes = self.stencils[key]
            for name, w, h in shapes:
                stencil_name = name.replace(" ", "_")
                if stencil_name in ignore:
                    continue
                words = self.m_getTagsForStencil([package, stencil_name])
                extra = tag_map.get(stencil_name)
                if extra not in (None, UNDEF):
                    words.append(to_str(extra))
                entries.append(Entry("vertex", stencil_name.replace("_", " "),
                                     self.m_filterTags([" ".join(words)]),
                                     js_round(w * scale), js_round(h * scale),
                                     "shape=" + package + stencil_name.lower() + style, ""))
        self._finish_palette(to_str(pid), title, entries)
