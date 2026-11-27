#!/usr/bin/env -S python3 -I
"""Extracts draw.io's shape palettes into resources/palettes/*.json.

Usage:
    python3 -I tools/palette/extract_palettes.py <webapp dir> <out dir> [--check]

<webapp dir> is upstream `src/main/webapp` (jgraph/drawio, Apache-2.0). The
sidebar definition scripts are read as *text* and interpreted by a small
JavaScript-subset interpreter (jsmini.py); no JavaScript is ever executed by
a JS engine. Output schema: docs/design/ui.md section 8. Besides the JSON
files the tool writes REPORT.md (entries written/skipped per palette with the
upstream file:line and reason of every skipped entry). README.md and
LICENSE.md in the output directory are maintained by hand.

--check regenerates into a temporary directory, diffs it against <out dir>
(exit 1 on any difference) and fails when the share of resolved entries in the
core families (general .. bpmn) drops below 90 %.
"""

import argparse
import filecmp
import json
import os
import re
import sys
import tempfile

sys.dont_write_bytecode = True  # importing tools/stencils must not leave __pycache__ there
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "stencils"))

import jsmini  # noqa: E402
import sidebar_host as host_mod  # noqa: E402
import stencil_pack  # noqa: E402
import vendor_stencils  # noqa: E402
from jsmini import Interpreter, NativeFn, UNDEF, Unsupported  # noqa: E402

COMMIT = "c72c9a98"
SOURCE_FILES = ["js/grapheditor/Sidebar.js"]
MIN_CORE_RATIO = 0.90

# (family file, order, palette function calls, source script under js/diagramly/sidebar/ or None)
# Calls are (prototype function name, [js args]); order is the draw.io Sidebar.init order
# (ui.md section 8). Palettes of multi-library families share the family order and sort by id.
FAMILIES = [
    ("general", 0, [("addGeneralPalette", [True])], None),
    ("misc", 1, [("addMiscPalette", [False])], None),
    ("advanced", 2, [("addAdvancedPalette", [False])], None),
    ("basic", 3, [("addBasicPalette", [])], "Basic"),
    ("arrows", 4, [("addArrows2Palette", [])], "Arrows2"),  # plus the 'arrows' stencil palette, see below
    ("flowchart", 5, [("addFlowchartPalette", [])], "Flowchart"),
    ("uml", 8, [("addUmlPalette", [False])], None),
    ("er", 9, [("addErPalette", [])], "ER"),
    ("bpmn", 10, [("addBpmn2Palette", [])], "BPMN"),
    ("network", 11, [("addNetworkPalette", [])], "Network"),
    ("kubernetes", 12, [("addKubernetesPalette", [])], "Kubernetes"),
    ("aws4", 13, [("addAWS4Palette", [])], "AWS4"),
    ("azure", 14, [("addAzure2Palette", [])], "Azure2"),
    ("gcp", 15, [("addGCP2Palette", [])], "GCP2"),
    ("mockups", 16, [("addMockupPalette", [])], "Mockup"),
    ("floorplan", 17, [("addFloorplanPalette", [])], "Floorplan"),
]
# Families whose palettes belong to a named library (Palette::library); core ones use "".
LIBRARIES = {"aws4": "aws4", "azure": "azure2", "gcp": "gcp2", "mockups": "mockups"}
CORE_FAMILIES = ["general", "misc", "advanced", "basic", "arrows", "flowchart", "uml", "er", "bpmn"]
# draw.io titles both arrow palettes "Arrows"; the second one is told apart here.
TITLE_OVERRIDES = {"arrows2": "Arrows 2"}
DERIVED = [("tables", 6, "Tables"), ("containers", 7, "Containers")]

# initPalettes (js/diagramly/sidebar/Sidebar.js:1481): the 'arrows' stencil palette
ARROWS_STYLE = ";html=1;verticalLabelPosition=bottom;verticalAlign=top;strokeWidth=2;strokeColor=#000000;"


class LazyStencils:
    """stencil file name -> (package name with trailing dot, [(shape name, w, h)]), decoded on demand."""

    def __init__(self, webapp):
        self.blobs = vendor_stencils.read_entries(webapp)
        self.cache = {}

    def __contains__(self, key):
        return key in self.blobs

    def __getitem__(self, key):
        if key not in self.cache:
            root = stencil_pack.decode_inflated(stencil_pack.inflate_blob(self.blobs[key]))
            shapes = [(s.attrs.get("name", ""), float(s.attrs.get("w", 80) or 80), float(s.attrs.get("h", 80) or 80))
                      for s in root.children if s.name == "shape"]
            self.cache[key] = (root.attrs.get("name", "").lower() + ".", shapes)
        return self.cache[key]


def read_text(webapp, rel):
    with open(os.path.join(webapp, rel), encoding="utf-8") as f:
        return f.read()


class Extractor:
    def __init__(self, webapp):
        self.webapp = webapp
        consts = host_mod.parse_constants(read_text(webapp, "mxgraph/src/util/mxConstants.js"))
        resources = host_mod.parse_resources(read_text(webapp, "resources/dia.txt"))
        self.resources = resources
        self.load_warnings = []
        self.host = None
        globals_ = {
            "mxConstants": host_mod.Namespace(dict(consts)),
            "mxResources": host_mod.Namespace({"get": NativeFn(lambda this, a: resources.get(jsmini.to_str(a[0]), jsmini.to_str(a[0])))}),
            "mxUtils": host_mod.make_mxutils(),
            "String": host_mod.StringFn(),
            "Math": host_mod.Namespace(host_mod.make_math()),
            "parseInt": NativeFn(host_mod.js_parse_int),
            "parseFloat": NativeFn(host_mod.js_parse_float),
            "isNaN": NativeFn(lambda this, a: jsmini.to_num(a[0]) != jsmini.to_num(a[0])),
            "mxCell": host_mod.Ctor(lambda a: host_mod.Cell(a[0] if a else "", a[1] if len(a) > 1 else None,
                                                             a[2] if len(a) > 2 else None)),
            "mxGeometry": host_mod.Ctor(lambda a: host_mod.Geometry(*[host_mod._num_arg(a, i) for i in range(4)])),
            "mxPoint": host_mod.Ctor(lambda a: host_mod.Point(host_mod._num_arg(a, 0), host_mod._num_arg(a, 1))),
            "mxRectangle": host_mod.Ctor(lambda a: host_mod.Geometry(*[host_mod._num_arg(a, i) for i in range(4)])),
            "mxClient": host_mod.Namespace({}),
            "Graph": host_mod.Namespace({"createInheritedLabelStyle": NativeFn(lambda this, a: "inheritText=1;"),
                                         "inheritLabelStyleKey": "inheritText"}),
            "urlParams": {},
            "STENCIL_PATH": "stencils",
            "GRAPH_IMAGE_PATH": "img",
        }
        globals_["Editor"] = host_mod.Namespace(self.editor_constants())
        self.interp = Interpreter(globals_, self.on_element_error)
        self.stencils = LazyStencils(webapp)

    def editor_constants(self):
        """`Editor.default*Style` literals from js/diagramly/Editor.js (text only)."""
        src = read_text(self.webapp, "js/diagramly/Editor.js")
        values = {}
        interp = Interpreter({})
        for m in re.finditer(r"^\tEditor\.(default\w*Style) = (.*?);\n", src, re.S | re.M):
            try:
                parser = jsmini.Parser(m.group(2), "Editor.js")
                values[m.group(1)] = interp.eval(parser.expression(), interp.global_scope)
            except Unsupported:
                pass
        return values

    def on_element_error(self, node, exc):
        if self.host is not None:
            self.host.note_skip(exc.pos or node.pos, exc.message)

    # -- script loading
    def load_script(self, rel):
        path = os.path.join(self.webapp, rel)
        program = jsmini.parse(read_text(self.webapp, rel), os.path.basename(rel))
        scope = self.interp.global_scope
        self.run_statements(program.body, scope, rel)

    def run_statements(self, body, scope, rel):
        self.interp.hoist(body, scope)
        for stmt in body:
            if stmt.kind == "expr" and stmt.expr.kind == "call" and stmt.expr.callee.kind == "function" \
                    and not stmt.expr.args:
                # (function() { ... })(): run the body statement by statement so one
                # unsupported statement does not hide the definitions after it.
                inner = jsmini.Scope(scope)
                self.run_statements(stmt.expr.callee.body.body, inner, rel)
                continue
            try:
                self.interp.exec(stmt, scope)
            except (Unsupported, jsmini.JSThrow) as exc:
                self.load_warnings.append("%s:%d: %s" % (os.path.basename(rel), stmt.pos[1],
                                                         getattr(exc, "message", exc)))

    def prototype(self):
        return self.interp.global_scope.vars["Sidebar"].props.setdefault("prototype", {})

    # -- extraction
    def run(self):
        self.load_script("js/grapheditor/Sidebar.js")
        for _name, _order, _calls, script in FAMILIES:
            if script:
                self.load_script("js/diagramly/sidebar/Sidebar-%s.js" % script)
        self.host = host_mod.SidebarHost(self.interp, self.prototype(), self.resources, self.stencils)
        families = {}
        for name, order, calls, _script in FAMILIES:
            start = len(self.host.palettes)
            if name == "arrows":
                self.host.m_addStencilPalette(["arrows", self.resources.get("arrows", "Arrows"),
                                               "stencils/arrows.xml", ARROWS_STYLE])
            for fn_name, args in calls:
                self.call_palette_function(fn_name, args)
            families[name] = self.collect(name, order, start)
        return families

    def call_palette_function(self, fn_name, args):
        fn = self.host.prototype.get(fn_name)
        if fn is None:
            self.host.note_skip(None, "palette function %s is not defined" % fn_name)
            return
        try:
            self.interp.call_value(fn, self.host, list(args))
        except (Unsupported, jsmini.JSThrow) as exc:
            self.host.note_skip(getattr(exc, "pos", None), "%s aborted: %s" % (fn_name, getattr(exc, "message", exc)))

    def collect(self, family, order, start):
        palettes = self.host.palettes[start:]
        leftover = self.host.pending_skips
        self.host.pending_skips = []
        if palettes and leftover:
            palettes[-1].skips.extend(leftover)
        elif leftover:
            palettes = [host_mod.Palette(family, family, "")]
            palettes[0].skips = leftover
        out = []
        for p in palettes:
            out.append(finish_palette(p, order, LIBRARIES.get(family, "")))
        out.sort(key=lambda p: p["id"])
        return out


# --------------------------------------------------------------------------
# Entry records
# --------------------------------------------------------------------------

def num(v):
    r = round(float(v), 4)
    return int(r) if r == int(r) else r


def _is_default_cell(c):
    f = c.fields
    return (f["connectable"] is True and f["visible"] is True and not f["collapsed"] and not f["children"]
            and f["source"] is None and f["target"] is None and isinstance(f["value"], str)
            and isinstance(f["style"], (str, type(None))))


def _is_origin_geometry(g, w, h):
    f = g.fields
    return (f["x"] == 0 and f["y"] == 0 and f["width"] == w and f["height"] == h and not f["relative"]
            and f["offset"] is None and not f["points"] and f["sourcePoint"] is None and f["targetPoint"] is None
            and f["alternateBounds"] is None)


def simplify(entry):
    """A one-cell template that is exactly what createVertexTemplate/createEdgeTemplate builds."""
    if entry.cells is None or len(entry.cells) != 1:
        return None
    c = entry.cells[0]
    g = c.fields["geometry"]
    if not isinstance(g, host_mod.Geometry) or not _is_default_cell(c) or c.fields["style"] is None:
        return None
    w, h = entry.w, entry.h
    if c.fields["vertex"] and not c.fields["edge"] and _is_origin_geometry(g, w, h):
        return "vertex", c
    f = g.fields
    sp, tp = f["sourcePoint"], f["targetPoint"]
    if (c.fields["edge"] and not c.fields["vertex"] and f["relative"] and f["width"] == w and f["height"] == h
            and f["x"] == 0 and f["y"] == 0 and f["offset"] is None and not f["points"]
            and isinstance(sp, host_mod.Point) and isinstance(tp, host_mod.Point)
            and (sp.fields["x"], sp.fields["y"]) == (0, h) and (tp.fields["x"], tp.fields["y"]) == (w, 0)):
        return "edge", c
    return None


def entry_record(e):
    """JSON object of one entry (without id)."""
    rec = {"title": e.title, "w": num(e.w), "h": num(e.h), "tags": host_mod.normalise_tags(e.tags)}
    if e.title and e.title.lower() not in rec["tags"]:
        pass  # titles are searched separately by the loader
    if not e.show_label:
        rec["showLabel"] = False
    if e.kind in ("vertex", "edge"):
        rec["kind"] = e.kind
        rec["style"] = e.style
        if e.value:
            rec["value"] = e.value
        return rec
    if e.xml is not None:
        rec["kind"] = "cells"
        rec["xml"] = e.xml
        return rec
    simple = simplify(e)
    if simple is not None:
        kind, cell = simple
        rec["kind"] = kind
        rec["style"] = cell.fields["style"]
        if cell.fields["value"]:
            rec["value"] = cell.fields["value"]
        return rec
    rec["kind"] = "cells"
    rec["xml"] = host_mod.cells_to_xml(e.cells)
    return rec


def finish_palette(p, order, library):
    entries = []
    used = {}
    for e in p.entries:
        try:
            rec = entry_record(e)
        except Unsupported as exc:
            p.skips.append(("?", 0, "serialisation: " + exc.message))
            continue
        base = host_mod.slug(e.title)
        n = used.get(base, 0) + 1
        candidate = base if n == 1 else "%s-%d" % (base, n)
        while candidate in used:  # "Table 2" must not collide with the second "Table"
            n += 1
            candidate = "%s-%d" % (base, n)
        used[base] = n
        used[candidate] = 1
        rec["id"] = candidate
        entries.append(rec)
    pid = re.sub(r"\s+", "", p.id)
    return {"id": pid, "title": TITLE_OVERRIDES.get(pid, p.title), "library": library, "order": order,
            "entries": entries, "skips": p.skips}


# --------------------------------------------------------------------------
# Derived palettes (Tables, Containers)
# --------------------------------------------------------------------------

_TABLE_RE = re.compile(r"shape=table\b|childLayout=tableLayout|shape=tableRow")
_CONTAINER_RE = re.compile(r"(^|;)(swimlane|shape=swimlane)(;|$)|(^|;)container=1(;|$)")


def top_level_styles(rec):
    if rec["kind"] != "cells":
        return [rec.get("style", "")]
    return [m.group(1) for m in re.finditer(r'<mxCell id="\d+" value="[^"]*" style="([^"]*)"[^>]* parent="1"', rec["xml"])]


def derive(families, pattern, pid, order, title):
    entries = []
    for fam in ("general", "misc", "advanced"):
        for palette in families.get(fam, []):
            for rec in palette["entries"]:
                if any(pattern.search(s) for s in top_level_styles(rec)):
                    entries.append(dict(rec))
    # ids stay unique per palette: a collision between source palettes gets "-2", "-3", ...
    taken = set()
    for rec in entries:
        base, n = rec["id"], 1
        while rec["id"] in taken:
            n += 1
            rec["id"] = "%s-%d" % (base, n)
        taken.add(rec["id"])
    return {"id": pid, "title": title, "library": "", "order": order, "entries": entries, "skips": []}


# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------

def dump(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def write_family(path, palettes, files):
    lines = ['{"schema":1,"source":{"repo":"jgraph/drawio","commit":"%s","files":%s,"license":"Apache-2.0"},"palettes":[' %
             (COMMIT, dump(files))]
    plines = []
    for p in palettes:
        head = {k: v for k, v in p.items() if k not in ("entries", "skips")}
        head_s = dump(head)[:-1]  # drop closing brace
        body = ",\n".join(dump(e) for e in p["entries"])
        plines.append('%s,"entries":[\n%s\n]}' % (head_s, body))
    lines.append(",\n".join(plines))
    lines.append("]}\n")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def family_files(name):
    script = next(s for n, _o, _c, s in FAMILIES if n == name)
    files = list(SOURCE_FILES)
    if script:
        files.append("js/diagramly/sidebar/Sidebar-%s.js" % script)
    if name == "arrows":
        files.append("stencils/arrows.xml (js/stencils.min.js)")
    return files


def write_report(path, families, derived, warnings):
    lines = ["# Palette extraction report", "",
             "Generated by `tools/palette/extract_palettes.py` from jgraph/drawio `%s`. Do not edit." % COMMIT, "",
             "Entries *skipped* needed something outside the static subset (see `tools/palette/jsmini.py`); each is "
             "listed with the upstream `file:line` and the reason.", "",
             "| Family | Palette | Written | Skipped |", "|---|---|---:|---:|"]
    totals = {}
    for name in [f[0] for f in FAMILIES] + [d["id"] for d in derived]:
        written = skipped = 0
        for p in (families.get(name) or derived_map(derived).get(name, [])):
            lines.append("| %s | %s | %d | %d |" % (name, p["id"], len(p["entries"]), len(p["skips"])))
            written += len(p["entries"])
            skipped += len(p["skips"])
        totals[name] = (written, skipped)
    lines += ["", "## Totals per family", "", "| Family | Written | Skipped | Resolved |", "|---|---:|---:|---:|"]
    for name, (w, s) in totals.items():
        lines.append("| %s | %d | %d | %s |" % (name, w, s, ("%.1f %%" % (100.0 * w / (w + s))) if w + s else "-"))
    core_w = sum(totals[n][0] for n in CORE_FAMILIES)
    core_s = sum(totals[n][1] for n in CORE_FAMILIES)
    lines += ["", "Core families (general .. bpmn): %d written, %d skipped, %.1f %% resolved (minimum %d %%)." %
              (core_w, core_s, 100.0 * core_w / max(1, core_w + core_s), MIN_CORE_RATIO * 100), ""]
    lines += ["## Skipped entries", ""]
    any_skip = False
    for name in families:
        for p in families[name]:
            if p["skips"]:
                any_skip = True
                lines.append("### %s / %s" % (name, p["id"]))
                lines.append("")
                for file, line, reason in p["skips"]:
                    lines.append("- `%s:%s` %s" % (file, line, reason.replace("\n", " ")[:200]))
                lines.append("")
    if not any_skip:
        lines.append("None.")
    if warnings:
        lines += ["## Script load warnings", "",
                  "Top-level statements of the definition scripts that the interpreter could not run (they define no "
                  "palette content).", ""]
        lines += ["- `%s`" % w.replace("\n", " ")[:200] for w in warnings]
        lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return core_w, core_s


def derived_map(derived):
    return {p["id"]: [p] for p in derived}


def generate(webapp, out_dir):
    ex = Extractor(webapp)
    families = ex.run()
    derived = [derive(families, _TABLE_RE, "tables", 6, "Tables"),
               derive(families, _CONTAINER_RE, "containers", 7, "Containers")]
    os.makedirs(out_dir, exist_ok=True)
    for name, palettes in families.items():
        write_family(os.path.join(out_dir, name + ".json"), palettes, family_files(name))
    for p in derived:
        write_family(os.path.join(out_dir, p["id"] + ".json"), [p],
                     ["derived from general.json, misc.json, advanced.json"])
    core_w, core_s = write_report(os.path.join(out_dir, "REPORT.md"), families, derived, ex.load_warnings)
    return families, derived, core_w / max(1, core_w + core_s)


def generated_names(families, derived):
    return sorted(["%s.json" % n for n in families] + ["%s.json" % p["id"] for p in derived] + ["REPORT.md"])


def summary(families, derived):
    for name, palettes in list(families.items()) + [(p["id"], [p]) for p in derived]:
        w = sum(len(p["entries"]) for p in palettes)
        s = sum(len(p["skips"]) for p in palettes)
        print("%-12s palettes %3d  written %5d  skipped %4d" % (name, len(palettes), w, s))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("webapp", help="upstream src/main/webapp directory")
    parser.add_argument("out", help="output directory (resources/palettes)")
    parser.add_argument("--check", action="store_true", help="regenerate into a temp dir and diff against <out>")
    args = parser.parse_args(argv)

    if not args.check:
        families, derived, ratio = generate(args.webapp, args.out)
        summary(families, derived)
        print("core resolved ratio: %.1f %%" % (100 * ratio))
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        families, derived, ratio = generate(args.webapp, tmp)
        status = 0
        if ratio < MIN_CORE_RATIO:
            print("FAIL: core families resolve only %.1f %% (< %d %%)" % (100 * ratio, MIN_CORE_RATIO * 100))
            status = 1
        for name in generated_names(families, derived):
            have = os.path.join(args.out, name)
            if not os.path.exists(have) or not filecmp.cmp(os.path.join(tmp, name), have, shallow=False):
                print("DIFF: %s differs from the regenerated output" % name)
                status = 1
        if status == 0:
            print("palettes up to date (core resolved %.1f %%)" % (100 * ratio))
        return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
