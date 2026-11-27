"""Edge-case families: encodings, pages, layers, containers, edges, wrappers,
labels, styles, foreign XML, stress, pathological documents and bare libraries."""

from __future__ import annotations

import random

from xmlkit import (MODEL_DEFAULT, MXFILE_DESKTOP, MXFILE_OLD, MXFILE_WEB, Page, compress_page, el, geo, mxfile,
                    png_data_uri, pt, esc_attr, esc_text)

BOX = "rounded=0;whiteSpace=wrap;html=1;"
ROUND = "rounded=1;whiteSpace=wrap;html=1;"
ORTHO = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;"


def _small(p: Page, n: int = 4, label: str = "Node") -> list[str]:
    """A tiny chain of n boxes; shared body for encoding/page-attribute tests."""
    ids = [p.v(f"{label} {i + 1}", 40 + i * 160, 60, 120, 60, ROUND) for i in range(n)]
    for a, b in zip(ids, ids[1:]):
        p.e(a, b, "", ORTHO)
    return ids


# --------------------------------------------------------------------------- encodings

def encodings(emit):
    fam = "encoding"
    for enc, tag in (("plain", "inline-model"), ("deflate", "compressed-uri"), ("legacy", "legacy-compressed-no-uri"),
                     ("wrapped", "compressed-whitespace-wrapped")):
        p = Page("Page-1", seed=1400, enc=enc); _small(p)
        emit(fam, f"single-{enc}", mxfile([p]), [tag])
    # attribute compressed on mxfile
    for val in ("true", "false"):
        p = Page("Page-1", seed=1401, enc="deflate" if val == "true" else "plain"); _small(p)
        emit(fam, f"mxfile-compressed-{val}", mxfile([p], {**MXFILE_DESKTOP, "compressed": val}), [f"mxfile-compressed={val}"])
    # mismatch: compressed="false" but page is compressed; compressed="true" but inline
    p = Page("Page-1", seed=1402, enc="deflate"); _small(p)
    emit(fam, "attr-says-plain-page-compressed", mxfile([p], {**MXFILE_DESKTOP, "compressed": "false"}), ["compressed-attr-mismatch", "compressed"])
    p = Page("Page-1", seed=1403, enc="plain"); _small(p)
    emit(fam, "attr-says-compressed-page-plain", mxfile([p], {**MXFILE_DESKTOP, "compressed": "true"}), ["compressed-attr-mismatch"])
    # mixed, one of each
    ps = [Page(f"{e}", seed=1410 + i, enc=e) for i, e in enumerate(["plain", "deflate", "legacy", "wrapped", "plain", "deflate"])]
    for pg in ps:
        _small(pg, 3, pg.name)
    emit(fam, "mixed-six-pages", mxfile(ps), ["mixed-encodings", "multi-page", "compressed", "legacy-compressed"])
    # compressed with non-ASCII content (CJK/emoji need correct UTF-8 + percent-encoding)
    p = Page("日本語", seed=1420, enc="deflate")
    p.v("日本語のテキスト \U0001F600 – “quotes” ‘x’ é ñ ü", 40, 40, 300, 60, ROUND)
    p.v("a b+c%d/e?f=g&h#i", 40, 140, 300, 60, ROUND)
    emit(fam, "compressed-unicode-and-reserved-chars", mxfile([p]), ["compressed", "unicode", "uri-reserved-chars"])
    # xml declaration, BOM, CRLF
    p = Page("Page-1", seed=1430, pretty=True); _small(p)
    s = mxfile([p], nl=True, decl=True)
    emit(fam, "utf8-bom", b"\xef\xbb\xbf" + s.encode(), ["bom", "xml-declaration", "pretty-printed"])
    emit(fam, "crlf-line-endings", s.replace("\n", "\r\n"), ["crlf", "pretty-printed", "xml-declaration"])
    emit(fam, "no-trailing-newline-no-decl", mxfile([Page(seed=1431)]).rstrip(), ["no-decl"])
    # single-quoted attributes + char refs + &apos;
    emit(fam, "single-quoted-attributes-charrefs",
         "<mxfile host='app.diagrams.net'><diagram name='Q &amp; A &#x41;&#66;' id='q1'>"
         "<mxGraphModel dx='800' dy='600' grid='1' gridSize='10'><root><mxCell id='0'/><mxCell id='1' parent='0'/>"
         "<mxCell id='2' value='it&apos;s &quot;quoted&quot; &#x263A;' style='rounded=1;' vertex='1' parent='1'>"
         "<mxGeometry x='10' y='10' width='100' height='40' as='geometry'/></mxCell></root></mxGraphModel></diagram></mxfile>",
         ["single-quotes", "char-refs"])
    # extra whitespace between attributes and in tags
    emit(fam, "irregular-whitespace-in-tags",
         '<mxfile   host = "x"\n   version="1" >\n<diagram\tname="P"   id="w1" >\n<mxGraphModel  dx="1" dy="2"  >\n<root >\n'
         '<mxCell id="0" />\n<mxCell  id="1"  parent="0"  />\n<mxCell id="2" value="v"  vertex="1" parent="1"\n  ><mxGeometry x="0" y="0" width="10" height="10" as="geometry" /></mxCell>\n'
         '</root>\n</mxGraphModel>\n</diagram>\n</mxfile>\n', ["whitespace", "pretty-printed"])


# --------------------------------------------------------------------------- pages / attributes

def pages(emit):
    fam = "pages"
    for n in (2, 3, 5, 8, 12):
        ps = [Page(f"Page-{i + 1}", seed=1500 + i, enc=["plain", "deflate"][i % 2], ids="rand" if i % 3 == 0 else "seq") for i in range(n)]
        for i, pg in enumerate(ps):
            _small(pg, 2 + i % 4, f"P{i + 1}-")
        emit(fam, f"{n}-pages", mxfile(ps), ["multi-page", "mixed-encodings"] + (["max-12-pages"] if n == 12 else []))
    # page attribute variants
    variants = {
        "background-and-no-grid": {**MODEL_DEFAULT, "grid": 0, "background": "#FFF2CC"},
        "a3-landscape": {**MODEL_DEFAULT, "pageWidth": 1654, "pageHeight": 1169, "landscape": "1", "pageScale": 0.5},
        "grid-size-5-math-shadow": {**MODEL_DEFAULT, "gridSize": 5, "math": 1, "shadow": 1, "page": 0},
        "all-flags-off": {**MODEL_DEFAULT, "grid": 0, "guides": 0, "tooltips": 0, "connect": 0, "arrows": 0, "fold": 0, "page": 0},
        "minimal-model-attrs": {},
        "extreme-scroll-and-scale": {**MODEL_DEFAULT, "dx": -2400, "dy": 5000, "pageScale": 2.5, "pageWidth": 100000, "pageHeight": 0.5, "background": "none"},
    }
    for name, m in variants.items():
        p = Page("Page-1", seed=1510, model=m); _small(p)
        emit(fam, f"model-attrs-{name}", mxfile([p]), ["model-attrs"])
    # mxfile attribute variants
    for name, a in {"web": MXFILE_WEB, "desktop": MXFILE_DESKTOP, "old": MXFILE_OLD, "bare": {},
                    "extras": {**MXFILE_WEB, "scale": "1", "border": "0", "pages": "2", "type": "google", "modified": "2025-01-01T00:00:00.000Z", "compressed": "false"}}.items():
        p = Page("Page-1", seed=1520); _small(p)
        emit(fam, f"mxfile-attrs-{name}", mxfile([p], a), ["mxfile-attrs"])
    # page names
    names = ["Page &amp; <1>", "", "  spaced  ", "日本語", "Ünïcødé", 'quote"s', "a/b\\c", "emoji \U0001F600", "x" * 300, "line\nbreak"]
    ps = [Page(n, seed=1530 + i, pid=f"id-{i}") for i, n in enumerate(names)]
    for pg in ps:
        _small(pg, 1)
    emit(fam, "page-name-oddities", mxfile(ps), ["page-names", "multi-page", "unicode"])
    # duplicate page names, ids reused in different pages (ids are page-scoped)
    ps = [Page("Same", seed=1540 + i, pid=f"p{i}") for i in range(3)]
    for pg in ps:
        _small(pg, 2)
    emit(fam, "duplicate-names-and-cell-ids", mxfile(ps), ["duplicate-page-names", "same-ids-across-pages"])


# --------------------------------------------------------------------------- layers

def layers(emit):
    fam = "layers"
    p = Page("Layers", seed=1600, layer="L0")
    p.layer_attrs = {"value": "Background"}
    l2 = p.add_layer("Annotations"); l3 = p.add_layer("Hidden", visible=False); l4 = p.add_layer("Locked", locked=True)
    p.v("on background", 40, 40, 120, 50, ROUND, parent="L0")
    p.v("note", 200, 40, 120, 50, "shape=note;whiteSpace=wrap;html=1;", parent=l2)
    p.v("you cannot see me", 40, 140, 120, 50, ROUND, parent=l3)
    p.v("locked content", 200, 140, 120, 50, ROUND, parent=l4)
    emit(fam, "four-layers-hidden-locked", mxfile([p]), ["multi-layer", "visible=0", "locked=1", "custom-default-layer-id"])

    p = Page("Cross-layer", seed=1601, ids="rand")
    lb = p.add_layer("Back"); lf = p.add_layer("Front")
    a = p.v("A", 40, 40, 100, 50, ROUND, parent=lb); b = p.v("B", 300, 40, 100, 50, ROUND, parent=lf)
    p.e(a, b, "crosses layers", ORTHO, parent=lf)
    p.e(b, a, "edge in other layer", ORTHO, parent=lb, points=[(350, 150), (90, 150)])
    emit(fam, "edges-across-layers", mxfile([p]), ["multi-layer", "cross-layer-edge"])

    p = Page("Many layers", seed=1602, enc="deflate")
    for i in range(20):
        lid = p.add_layer(f"Layer {i}", visible=(i % 4 != 0), locked=(i % 5 == 0))
        p.v(f"in {i}", 20 + (i % 5) * 130, 20 + (i // 5) * 80, 110, 50, ROUND, parent=lid)
    emit(fam, "twenty-layers-compressed", mxfile([p]), ["multi-layer", "compressed", "visible=0", "locked=1"])

    p = Page("Unicode layers", seed=1603)
    for nm in ["图层", "طبقة", "שכבה", "Слой \U0001F5C2", "Layer \"q\" & <x>"]:
        lid = p.add_layer(nm); p.v(nm, 40, 40 + len(p.layers) * 70, 200, 50, ROUND, parent=lid)
    emit(fam, "unicode-layer-names", mxfile([p]), ["multi-layer", "unicode"])

    p = Page("Locked cells", seed=1604)
    p.v("locked=1 style", 40, 40, 120, 50, ROUND + "locked=1;")
    p.v("movable=0", 200, 40, 120, 50, ROUND + "movable=0;resizable=0;rotatable=0;deletable=0;editable=0;")
    p.v("connectable=0", 360, 40, 120, 50, ROUND, cell_attrs={"connectable": "0"})
    p.v("cell visible=0", 40, 140, 120, 50, ROUND, cell_attrs={"visible": "0"})
    emit(fam, "locked-and-hidden-cells", mxfile([p]), ["locked=1", "visible=0", "connectable=0"])

    p = Page("Empty layers", seed=1605)
    p.add_layer("Empty 1"); p.add_layer("Empty 2", visible=False)
    p.layer_attrs = {"visible": "0"}
    emit(fam, "empty-and-hidden-default-layer", mxfile([p]), ["multi-layer", "visible=0", "empty-layer"])


# --------------------------------------------------------------------------- containers / groups

def containers(emit):
    fam = "containers"
    p = Page("Groups", seed=1700)
    g = p.v("", 40, 40, 300, 120, "group", cell_attrs={"connectable": "0"})
    p.v("child 1", 0, 0, 140, 120, ROUND, parent=g); p.v("child 2", 160, 0, 140, 120, ROUND, parent=g)
    other = p.v("outside", 440, 70, 100, 60, ROUND)
    p.e(g, other, "to group", ORTHO)
    emit(fam, "simple-group", mxfile([p]), ["group", "connectable=0"])

    p = Page("Deep", seed=1701, enc="deflate")
    parent = None
    for d in range(8):
        parent = p.v(f"level {d}", 10 if d else 20, 30 if d else 20, 600 - d * 60, 400 - d * 40,
                     "swimlane;whiteSpace=wrap;html=1;container=1;collapsible=0;startSize=20;recursiveResize=0;", parent=parent)
    emit(fam, "nested-eight-deep", mxfile([p]), ["nested-containers", "compressed"])

    p = Page("Mixed", seed=1702)
    c = p.v("container=1", 30, 30, 300, 200, "rounded=0;whiteSpace=wrap;html=1;container=1;collapsible=0;dropTarget=1;pointerEvents=0;verticalAlign=top;")
    g = p.v("", 20, 40, 200, 100, "group", parent=c)
    p.v("inner", 10, 10, 80, 40, ROUND, parent=g)
    p.v("negative child", -30, -20, 60, 30, ROUND, parent=c)
    p.v("far child", 400, 300, 60, 30, ROUND, parent=c)
    emit(fam, "group-in-container-negative-offsets", mxfile([p]), ["group", "container", "negative-geometry", "child-outside-parent"])

    p = Page("Collapsed", seed=1703)
    c = p.v("collapsed container", 40, 40, 160, 40, "swimlane;whiteSpace=wrap;html=1;container=1;collapsible=1;startSize=40;", cell_attrs={"collapsed": "1"},
            geo_children=[el("mxRectangle", {"x": 40, "y": 40, "width": 300, "height": 200, "as": "alternateBounds"})])
    k = p.v("hidden child", 20, 60, 80, 40, ROUND, parent=c)
    o = p.v("outside", 400, 40, 80, 40, ROUND)
    p.e(k, o, "edge from hidden child", ORTHO)
    emit(fam, "collapsed-with-edge-from-child", mxfile([p]), ["collapsed=1", "alternateBounds", "edge-from-nested"])

    p = Page("Rotated group", seed=1704)
    g = p.v("", 100, 100, 200, 100, "group;rotation=30;")
    p.v("a", 0, 0, 100, 100, BOX, parent=g); p.v("b", 100, 0, 100, 100, BOX + "flipH=1;", parent=g)
    emit(fam, "rotated-group-flipped-child", mxfile([p]), ["group", "rotation", "flipH"])

    p = Page("Edge in container", seed=1705)
    c = p.v("c", 20, 20, 400, 200, "swimlane;container=1;startSize=20;html=1;")
    a = p.v("a", 20, 40, 80, 40, BOX, parent=c); b = p.v("b", 250, 100, 80, 40, BOX, parent=c)
    p.e(a, b, "inside", ORTHO, parent=c, points=[(60, 120)])
    p.e(None, None, "free inside", "endArrow=classic;html=1;", parent=c, sp=(20, 170), tp=(200, 170))
    emit(fam, "edges-parented-to-container", mxfile([p]), ["edge-in-container", "points-relative-to-parent"])


# --------------------------------------------------------------------------- edges

def edges(emit):
    fam = "edges"
    # waypoints
    p = Page("Waypoints", seed=1800)
    a = p.v("A", 40, 40, 80, 40, BOX); b = p.v("B", 500, 300, 80, 40, BOX)
    p.e(a, b, "many points", "edgeStyle=none;html=1;rounded=1;arcSize=30;",
        points=[(200, 60), (200, 160), (300, 160), (300, 260), (420, 260), (420, 320)])
    p.e(a, b, "float points", "html=1;curved=1;", points=[(120.5, 200.25), (333.333, 99.9)])
    p.e(a, b, "negative points", "html=1;", points=[(-50, -50), (-10.5, 400)])
    emit(fam, "waypoints", mxfile([p]), ["Array=points", "negative-geometry", "float-geometry"])

    # source/target points, no terminals
    p = Page("Free", seed=1801)
    for i in range(5):
        p.e(None, None, f"free {i}", "endArrow=block;html=1;", sp=(40, 40 + i * 50), tp=(300 + i * 20, 60 + i * 50))
    emit(fam, "free-edges-source-target-points", mxfile([p]), ["sourcePoint", "targetPoint", "dangling-edge"])

    # exit/entry constraints grid
    p = Page("Constraints", seed=1802)
    a = p.v("A", 200, 200, 100, 100, BOX)
    for i, (ex, ey) in enumerate([(0, 0), (0.5, 0), (1, 0), (1, 0.5), (1, 1), (0.5, 1), (0, 1), (0, 0.5)]):
        t = p.v(f"T{i}", 40 + (i % 4) * 150, 380 if i >= 4 else 20, 60, 40, ROUND)
        p.e(a, t, "", f"html=1;exitX={ex};exitY={ey};exitDx=0;exitDy=0;exitPerimeter=0;entryX=0.5;entryY=0.5;entryDx={i};entryDy=-{i};entryPerimeter=0;")
    emit(fam, "exit-entry-constraints", mxfile([p]), ["exitX/exitY", "entryX/entryY", "dx-dy", "perimeter=0"])

    # edge to edge
    p = Page("Edge2Edge", seed=1803)
    a = p.v("A", 40, 40, 80, 40, BOX); b = p.v("B", 400, 40, 80, 40, BOX); c = p.v("C", 220, 240, 80, 40, BOX)
    e1 = p.e(a, b, "", "html=1;endArrow=none;")
    p.e(c, e1, "to edge", "html=1;endArrow=classic;", tp=(260, 60))
    p.e(e1, c, "from edge", "html=1;", sp=(150, 60))
    emit(fam, "edge-to-edge", mxfile([p]), ["edge-to-edge"])

    # label variants: relative positions + child label cells
    p = Page("EdgeLabels", seed=1804)
    a = p.v("A", 40, 100, 80, 40, BOX); b = p.v("B", 500, 100, 80, 40, BOX)
    e = p.e(a, b, "main label", "html=1;", label_pos=(0.25, 15), offset=(5, -5))
    p.edge_label(e, "start", -0.9, 0, (0, -15)); p.edge_label(e, "middle", 0, 0, (0, 20)); p.edge_label(e, "end", 0.9, 0, (0, -15))
    emit(fam, "edge-label-children", mxfile([p]), ["relative-label", "offset", "edgeLabel-cells"])

    # all arrow markers
    p = Page("Arrows", seed=1805, enc="deflate")
    markers = ["classic", "classicThin", "block", "blockThin", "open", "openThin", "oval", "diamond", "diamondThin", "dash", "cross", "async",
               "halfCircle", "box", "doubleBlock", "ERone", "ERmany", "ERmandOne", "ERzeroToOne", "ERoneToMany", "ERzeroToMany", "circlePlus", "none", "baseDash"]
    for i, m in enumerate(markers):
        y = 30 + i * 28
        p.e(None, None, m, f"endArrow={m};startArrow={markers[(i + 3) % len(markers)]};endFill={i % 2};startFill={(i + 1) % 2};endSize=10;startSize=8;html=1;", sp=(40, y), tp=(300, y))
    emit(fam, "arrow-markers-compressed", mxfile([p]), ["compressed", "markers"])

    # edge styles
    p = Page("EdgeStyles", seed=1806)
    a = p.v("A", 40, 40, 80, 40, BOX); b = p.v("B", 400, 200, 80, 40, BOX)
    for i, st in enumerate(["edgeStyle=orthogonalEdgeStyle", "edgeStyle=elbowEdgeStyle;elbow=horizontal", "edgeStyle=elbowEdgeStyle;elbow=vertical",
                            "curved=1", "edgeStyle=entityRelationEdgeStyle", "edgeStyle=isometricEdgeStyle;elbow=horizontal", "edgeStyle=segmentEdgeStyle",
                            "shape=link", "shape=flexArrow", "shape=arrow", "dashed=1;dashPattern=1 4;strokeWidth=3", "jumpStyle=arc;jumpSize=12", "rounded=1;arcSize=0", "strokeColor=none"]):
        p.e(a, b, st.split(";")[0], f"html=1;{st};", offset=(0, i))
    emit(fam, "edge-styles", mxfile([p]), ["edgeStyle", "shape=link", "flexArrow"])

    # self loops and parallel edges
    p = Page("Loops", seed=1807)
    a = p.v("A", 100, 100, 80, 40, BOX); b = p.v("B", 400, 100, 80, 40, BOX)
    for i in range(4):
        p.e(a, a, f"self {i}", ORTHO, points=[(60 - i * 20, 40 - i * 20), (60 - i * 20, 120)])
    for i in range(4):
        p.e(a, b, f"par {i}", "html=1;curved=1;", points=[(300, 20 + i * 60)])
    emit(fam, "self-loops-and-parallel", mxfile([p]), ["self-loop", "parallel-edges"])

    # jumps: crossing edges
    p = Page("Jumps", seed=1808)
    for i in range(5):
        p.e(None, None, "", "html=1;jumpStyle=arc;jumpSize=10;", sp=(40, 60 + i * 30), tp=(400, 60 + i * 30))
        p.e(None, None, "", "html=1;jumpStyle=gap;", sp=(80 + i * 60, 20), tp=(80 + i * 60, 240))
    emit(fam, "line-jumps", mxfile([p]), ["jumpStyle", "free-edges-source-target-points"])

    # edge with HTML label & fonts & label background
    p = Page("StyledLabel", seed=1809)
    a = p.v("A", 40, 40, 80, 40, BOX); b = p.v("B", 300, 40, 80, 40, BOX)
    p.e(a, b, "<b>bold</b> <font color=\"#ff0000\">red</font>", "html=1;labelBackgroundColor=#ffffff;fontSize=14;fontStyle=3;align=left;verticalAlign=top;labelBorderColor=#000000;spacing=4;")
    emit(fam, "edge-html-label-styles", mxfile([p]), ["html-label"])


# --------------------------------------------------------------------------- wrappers (object / UserObject)

def objects(emit):
    fam = "objects"
    p = Page("Objects", seed=1900)
    custom = {"owner": "Alice", "status": "active", "cost": "1200.50", "empty": "", "multi": "line1\nline2\ttabbed"}
    p.v("Server %owner%", 40, 40, 160, 60, ROUND, obj=("object", {"placeholders": "1", **custom}))
    p.v("Plain wrapper", 240, 40, 160, 60, ROUND, obj=("UserObject", {"tooltip": "I am a tooltip &amp; more", "link": "https://example.com/?a=1&b=2"}))
    p.v("", 440, 40, 160, 60, ROUND, obj=("object", {"note": "label comes from value"}))
    emit(fam, "wrappers-basic", mxfile([p]), ["object", "UserObject", "placeholders=1", "tooltip", "link", "custom-attrs"])

    p = Page("Placeholders", seed=1901)
    p.v("<b>%name%</b><br>%role% (%missing%) %%literal%%", 40, 40, 200, 80, "rounded=1;whiteSpace=wrap;html=1;",
        obj=("object", {"placeholders": "1", "name": "Ada", "role": "Engineer"}))
    p.v("%name% without placeholders flag", 280, 40, 200, 80, ROUND, obj=("object", {"name": "Ignored"}))
    p.v("%date{yyyy}% %page% %pagenumber% %id%", 520, 40, 200, 80, ROUND, obj=("object", {"placeholders": "1"}))
    emit(fam, "placeholders", mxfile([p]), ["object", "placeholders=1", "builtin-placeholders"])

    p = Page("Links", seed=1902)
    for i, link in enumerate(["https://example.com", "page:2", "data:page/id,abc123", 'data:action/json,{"actions":[{"toggle":{"cells":["2"]}}]}',
                              "mailto:a@b.c?subject=hi%20there", "javascript:alert(1)", "file:///etc/hosts", "relative/path.drawio", "#anchor"]):
        p.v(f"link {i}", 40 + (i % 3) * 200, 40 + (i // 3) * 90, 160, 60, ROUND, obj=("UserObject", {"link": link}))
    emit(fam, "links", mxfile([p]), ["UserObject", "link", "data:action/json", "data:page/id"])

    p = Page("Odd attr names", seed=1903)
    odd = {"data-x": "1", "x.y": "2", "a_b": "3", "ns:attr": "4", "ünï": "5", "id2": "6", "style2": "7", "vertex": "wrapper-level vertex attr", "parent": "wrapper-level parent"}
    emit_p = p.v("odd names", 40, 40, 200, 60, ROUND, obj=("object", odd))
    emit(fam, "odd-attribute-names", mxfile([p], {**MXFILE_WEB, "xmlns:ns": "urn:corpus:ns"}), ["object", "custom-attrs", "namespaced-attr"])

    p = Page("Wrapped edge", seed=1904)
    a = p.v("A", 40, 40, 80, 40, BOX); b = p.v("B", 300, 40, 80, 40, BOX)
    p.e(a, b, "edge", "html=1;", obj=("object", {"weight": "5", "tooltip": "edge tooltip"}))
    g = p.v("", 40, 140, 200, 100, "group", obj=("object", {"kind": "group"}), cell_attrs={"connectable": "0"})
    p.v("in group", 0, 0, 100, 100, BOX, parent=g, obj=("UserObject", {"k": "v"}))
    emit(fam, "wrapped-edges-groups-children", mxfile([p]), ["object", "UserObject", "wrapped-edge", "group"])

    p = Page("Values", seed=1905)
    vals = ["", " ", "a&amp;b", "<script>", "'single'", '"double"', "emoji \U0001F600", "tab\there", "x" * 5000]
    for i, v in enumerate(vals):
        p.v(f"v{i}", 40, 40 + i * 70, 100, 50, ROUND, obj=("object", {"val": v}))
    emit(fam, "attribute-value-extremes", mxfile([p]), ["object", "custom-attrs", "long-value"])

    p = Page("Compressed objects", seed=1906, enc="deflate", ids="rand")
    for i in range(30):
        p.v("%n%", 40 + (i % 6) * 130, 40 + (i // 6) * 80, 110, 60, ROUND, obj=("object", {"placeholders": "1", "n": f"item {i}", "idx": str(i)}))
    emit(fam, "compressed-many-objects", mxfile([p]), ["object", "placeholders=1", "compressed"])


# --------------------------------------------------------------------------- labels

def labels(emit):
    fam = "labels"
    HTML = "text;html=1;whiteSpace=wrap;verticalAlign=middle;overflow=hidden;align=left;"
    cases = {
        "html-entities": ["&lt;b&gt;not bold&lt;/b&gt;", "<b>bold</b>", "a &amp; b &lt; c &gt; d", "&amp;lt; double escaped", "&nbsp;&nbsp;indent", "&#169; &#x00E9; &euro;"],
        "breaks-and-nesting": ["line1<br>line2", "line1<br/>line2", "<div>div1</div><div>div2</div>", "<b><i><u>all three</u></i></b>",
                               '<font color="#ff0000" style="font-size: 20px;">big <b>red</b></font>', '<span style="background-color:yellow">hi</span>', "<ul><li>one</li><li>two</li></ul>",
                               '<a href="https://example.com/?a=1&amp;b=2">link</a>', "<h1>heading</h1><p>para</p>", "<sup>sup</sup><sub>sub</sub>", "<pre>pre\n  formatted</pre>"],
        "unicode-scripts": ["日本語のテキスト、中文、한국어", "مرحبا بالعالم", "שלום עולם", "Привет мир", "Ελληνικά", "हिन्दी", "ภาษาไทย", "mixed: abc مرحبا 123 שלום def"],
        "emoji-and-combining": ["\U0001F600\U0001F680\U0001F4A1", "\U0001F468‍\U0001F469‍\U0001F467 family ZWJ", "\U0001F1EF\U0001F1F5 flag", "é ä ñ combining", "Z͑ͫ̓ͪ̂ͫ̽͏̴̙̤̞͉͚̯̞̠͍A zalgo",
                                "\U0001D11E clef (astral)", "​ zero-width space ‍", "‮ RLO override ‬", "﻿ BOM char"],
        "quotes-and-ampersands": ["it's", 'say "hi"', "`backtick`", "a&b", "&&&", "&amp;amp;", "'\"'\"", "<>", "&#10; literal text", "100%", "% %s %d"],
    }
    for k, (name, items) in enumerate(cases.items()):
        p = Page(name, seed=2000 + k, enc="deflate" if k == 3 else "plain")
        for i, t in enumerate(items):
            p.v(t, 20 + (i % 3) * 260, 20 + (i // 3) * 90, 240, 70, HTML)
        emit(fam, name, mxfile([p]), ["html-label"] + (["unicode"] if k in (2, 3) else []) + (["entities"] if k in (0, 4) else []) + (["compressed"] if k == 3 else []))
    # non-HTML labels with newlines
    p = Page("Newlines", seed=2010)
    for i, t in enumerate(["line1\nline2", "crlf1\r\nline2", "tab\tsep", "\nleading newline", "trailing\n", "a\n\nb", "  spaces  kept  ", "\n"]):
        p.v(t, 20 + (i % 4) * 190, 20 + (i // 4) * 90, 170, 70, "whiteSpace=wrap;html=0;")
    emit(fam, "newlines-non-html", mxfile([p]), ["newline-entity", "html=0", "whitespace"])
    # long label
    p = Page("Long", seed=2011, enc="deflate")
    p.v(" ".join(f"word{i}" for i in range(3000)), 20, 20, 400, 300, "whiteSpace=wrap;html=1;overflow=hidden;")
    p.v("<br>".join(f"<b>{i}</b> row" for i in range(500)), 450, 20, 300, 300, "whiteSpace=wrap;html=1;overflow=fill;")
    emit(fam, "very-long-labels", mxfile([p]), ["long-value", "compressed"])
    # math
    p = Page("Math", seed=2012, model={**MODEL_DEFAULT, "math": 1})
    for i, t in enumerate(["$$\\sum_{i=0}^n i^2 = \\frac{n(n+1)(2n+1)}{6}$$", "\\(x^2 + y^2 = z^2\\)", "#MathJax $$a<b>c$$ &amp; \\\\", "{{{x}}}"]):
        p.v(t, 20, 20 + i * 80, 400, 60, "text;html=1;align=center;verticalAlign=middle;")
    emit(fam, "mathjax-labels", mxfile([p]), ["math=1", "backslashes"])
    # text styles
    p = Page("TextStyles", seed=2013)
    for i, st in enumerate(["fontStyle=1", "fontStyle=2", "fontStyle=4", "fontStyle=7", "fontSize=6", "fontSize=72", "textOpacity=50", "rotation=90;horizontal=0",
                            "verticalAlign=top;align=left;spacing=20", "labelPosition=right;align=left;verticalLabelPosition=bottom;verticalAlign=top", "fontFamily=Courier New;fontColor=#00FF00;labelBackgroundColor=#000000",
                            "whiteSpace=nowrap;overflow=visible", "textDirection=rtl", "glass=1;shadow=1"]):
        p.v("مرحبا Text" if "rtl" in st else "Text", 20 + (i % 5) * 150, 20 + (i // 5) * 110, 130, 80, f"html=1;{st};")
    emit(fam, "text-style-variants", mxfile([p]), ["rtl", "font-styles", "rotation"])


# --------------------------------------------------------------------------- styles

def styles(emit):
    fam = "styles"
    cases = {
        "bare-tokens": ["ellipse", "ellipse;", "ellipse;whiteSpace=wrap;html=1;", "text;html=1", "rounded;fillColor=red", "a;b;c;d=e;f"],
        "semicolons": ["a=1", "a=1;", "a=1;;b=2", ";a=1", ";;;", ";", "a=1;b=2;;"],
        "empty-values": ["fillColor=;", "fillColor=;strokeColor=#000", "=value", "=", "a==b", "a=b=c;d", "key="],
        "whitespace": ["  rounded=1  ;  fillColor = red ; ", "rounded=1;\tfillColor=red", "a=b c d;e=f g", " ; ", "a=1 ;b = 2"],
        "duplicates-and-case": ["fillColor=red;fillColor=blue", "FillColor=red;fillcolor=blue", "rounded=1;rounded=0;rounded", "text;text;text"],
        "unknown-keys": ["foo=bar;x-y=z;a.b=c;ns:key=v;_underscore=1", "customProp={\"json\":[1,2,3]};other=1", "u=ünï;e=\U0001F600", "k=a&b;l=<>", "k=\"q\";l='s'"],
        "special-values": ["points=[[0,0],[0.5,1],[1,0.5]];html=1", "dashPattern=8 8;dashed=1", "fontFamily=Helvetica, Arial, sans-serif", "fontSource=https://fonts.googleapis.com/css?family=Roboto&display=swap",
                           "gradientColor=#FFFFFF;fillStyle=zigzag-line;hachureGap=4", 'newEdgeStyle={"curved":0,"rounded":0};shape=umlLifeline', "strokeColor=light-dark(#000000,#FFFFFF)", "fillColor=default;strokeColor=inherit;fontColor=none", "arcSize=1e2;strokeWidth=.5;opacity=-1"],
    }
    for k, (name, items) in enumerate(cases.items()):
        p = Page(name, seed=2100 + k, enc="deflate" if k == 5 else "plain")
        for i, st in enumerate(items):
            p.v(f"s{i}", 20 + (i % 4) * 190, 20 + (i // 4) * 90, 170, 70, st)
        emit(fam, name, mxfile([p]), ["style-tokens"] + (["compressed"] if k == 5 else []))
    # missing / empty style attribute on vertices and edges
    p = Page("NoStyle", seed=2110)
    a = p.v("no style attr", 20, 20, 120, 50, None); b = p.v("empty style attr", 200, 20, 120, 50, "")
    p.e(a, b, "no style edge", None); p.e(b, a, "empty style edge", "", points=[(260, 120), (80, 120)])
    emit(fam, "missing-and-empty-style", mxfile([p]), ["no-style-attr", "empty-style"])
    # images
    p = Page("Images", seed=2111)
    p.v("png", 20, 20, 100, 100, "shape=image;html=1;verticalLabelPosition=bottom;verticalAlign=top;imageAspect=0;aspect=fixed;image=" + png_data_uri(3, 3, (10, 200, 90)))
    svg = "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><circle cx='5' cy='5' r='4' fill='red'/></svg>"
    import base64
    p.v("svg", 160, 20, 100, 100, "shape=image;html=1;image=data:image/svg+xml," + base64.b64encode(svg.encode()).decode())
    p.v("relative", 300, 20, 100, 100, "image;html=1;image=img/lib/clip_art/people/Worker_Man_128x128.png;")
    p.v("https", 440, 20, 100, 100, "shape=image;html=1;image=https://example.com/a.png?x=1&y=2;")
    p.v("clip", 580, 20, 100, 100, "shape=image;html=1;image=data:image/png,AAAA;imageBackground=#fff;imageBorder=#000;clipPath=inset(0% 0% 0% 0%);")
    emit(fam, "image-data-uris", mxfile([p]), ["image=data:", "image=relative-path", "image=https", "long-style"])
    # stencil
    stencil = ('<shape name="corpus" h="100" w="100" aspect="variable" strokewidth="inherit"><connections><constraint x="0.5" y="0" perimeter="1"/></connections>'
               '<background><path><move x="0" y="100"/><line x="50" y="0"/><line x="100" y="100"/><close/></path></background>'
               '<foreground><fillstroke/><ellipse x="40" y="40" w="20" h="20"/><stroke/></foreground></shape>')
    p = Page("Stencils", seed=2112)
    for i in range(3):
        p.v(f"stencil {i}", 20 + i * 160, 20, 120, 100, f"shape=stencil({compress_page(stencil.replace('corpus', f'corpus{i}'))});whiteSpace=wrap;html=1;")
    p.v("mxgraph names", 520, 20, 120, 100, "shape=mxgraph.some.unknown_shape;html=1;")
    emit(fam, "stencil-base64", mxfile([p]), ["shape=stencil", "base64-with-equals", "shape=mxgraph.*"])
    # mxgraph shape name sampler
    p = Page("mxgraph", seed=2113)
    names = ["mxgraph.flowchart.decision", "mxgraph.basic.cloud_callout", "mxgraph.arrows2.arrow", "mxgraph.electrical.resistors.resistor_1", "mxgraph.lean_mapping.manufacturing_process",
             "mxgraph.ios7.icons.cloud", "mxgraph.sitemap.page", "mxgraph.signs.healthcare.hospital", "mxgraph.pid.valves.gate_valve", "mxgraph.eip.message_channel",
             "mxgraph.azure.cloud", "mxgraph.cabinets.cabinet", "mxgraph.atlassian.jira", "mxgraph.office.concepts.cloud", "mxgraph.veeam.2d.proxy"]
    for i, n in enumerate(names):
        p.v(n.rsplit(".", 1)[-1], 20 + (i % 5) * 140, 20 + (i // 5) * 100, 100, 70, f"shape={n};html=1;")
    emit(fam, "mxgraph-shape-names", mxfile([p]), ["shape=mxgraph.*"])
    # very long styles
    p = Page("LongStyle", seed=2114)
    rng = random.Random(2114)
    long_unknown = ";".join(f"k{i}=v{rng.randrange(10**6)}" for i in range(2500)) + ";"
    p.v("long unknown keys", 20, 20, 200, 80, "rounded=1;" + long_unknown)
    p.v("long value", 260, 20, 200, 80, "fontFamily=" + ",".join(f"Font{i}" for i in range(3000)) + ";html=1;")
    p.v("long token", 500, 20, 200, 80, "x" * 20000)
    emit(fam, "very-long-styles", mxfile([p]), ["long-style"])
    # style on the mxCell wrapper's layer, root cell styles
    p = Page("RootStyle", seed=2115)
    p.zero_attrs = {"style": "unknown=on-cell-0;"}; p.layer_attrs = {"style": "locked=1;fillColor=red;"}
    p.v("x", 20, 20, 100, 50, ROUND)
    emit(fam, "styles-on-root-cells", mxfile([p]), ["style-on-layer", "style-on-root"])


# --------------------------------------------------------------------------- foreign / unknown XML

def _foreign_doc(h: set, enc: str = "plain") -> str:
    def H(name: str, s: str) -> str:
        return s if name in h else ""

    ua = lambda tag: H("attrs", f' x-unknown-{tag}="1" data-flag="yes" z:q="ns"')
    cmt = lambda t: H("comments", f"<!-- {t} -->")
    pi = lambda t: H("pi", f'<?qraw-test {t}?>')
    cdata = H("cdata", '<x-cdata><![CDATA[<raw & "text"> with ]]]]><![CDATA[> split terminator]]></x-cdata>')
    mixed = H("mixed", '<x-mixed>text <b a="1">bold<i>deep</i></b> tail<!--c--> more <?pi x?> end</x-mixed>')
    nsel = H("ns", '<z:meta z:id="7" xmlns:y="urn:y"><y:child y:a="1">ns text</y:child></z:meta>')
    ent = H("entities", '<x-ent a="&lt;&gt;&amp;&quot;&apos;&#10;&#9;&#x263A;">&lt;text&gt; &amp; &#169;</x-ent>')
    nsdecl = ' xmlns:z="urn:corpus:z"' if ("ns" in h or "attrs" in h) else ""
    cell_kids = H("cellchild", '<x-cellinfo kind="a"><x-nested>t</x-nested></x-cellinfo>')
    geo_kids = H("geochild", '<x-geoinfo a="1"/>')
    model = (
        f'<mxGraphModel dx="800" dy="600" grid="1" gridSize="10" page="1" pageWidth="827" pageHeight="1169"{nsdecl if enc == "deflate" else ""}{ua("model")}>'
        f'{H("modelchild", "<x-modelinfo>m</x-modelinfo>")}{cmt("in model")}'
        f'<root{ua("root")}>{H("modelchild", "<x-rootinfo r=\"1\"/>")}{pi("in root")}'
        f'<mxCell id="0"{ua("cell0")}>{cell_kids}</mxCell><mxCell id="1" parent="0"{ua("layer")}/>{cmt("between cells")}'
        f'<mxCell id="2" value="A" style="rounded=1;" vertex="1" parent="1"{ua("vertex")}>'
        f'{cell_kids}<mxGeometry x="40" y="40" width="120" height="60" as="geometry"{ua("geo")}>{geo_kids}{mixed}</mxGeometry>{cdata}</mxCell>'
        f'<object label="B" id="3" custom="1"{ua("object")}><mxCell style="rounded=1;" vertex="1" parent="1"{ua("objcell")}>'
        f'<mxGeometry x="240" y="40" width="120" height="60" as="geometry"/>{nsel}</mxCell>{ent}</object>'
        f'<UserObject label="C" id="4"{ua("uo")}><mxCell style="ellipse;" vertex="1" parent="1"><mxGeometry x="440" y="40" width="80" height="60" as="geometry"/></mxCell></UserObject>'
        f'<mxCell id="5" value="edge" edge="1" parent="1" source="2" target="3"{ua("edge")}>'
        f'<mxGeometry relative="1" as="geometry"{ua("egeo")}>{geo_kids}<mxPoint x="1" y="2" as="sourcePoint"{ua("pt")}/>'
        f'<Array as="points"{ua("array")}>{geo_kids}<mxPoint x="200" y="150"{ua("pt")}/>{cmt("in array")}</Array>'
        f'<mxRectangle x="0" y="0" width="1" height="1" as="alternateBounds"{ua("rect")}/>{geo_kids}</mxGeometry></mxCell>'
        f'{H("modelchild", "<x-roottail/>")}{cmt("end of root")}</root>{H("modelchild", "<x-modeltail/>")}</mxGraphModel>')
    if enc == "deflate":
        body = compress_page(model)
        diag_post = ""
    else:
        body = model
        diag_post = H("filechild", '<x-diagraminfo d="1"/>')
    head = ('<?xml version="1.0" encoding="UTF-8"?>\n' + H("pi", '<?xml-stylesheet type="text/xsl" href="x.xsl"?>\n') + H("comments", "<!-- top-level comment -->\n"))
    return (f'{head}<mxfile host="corpus" version="1"{nsdecl}{ua("mxfile")}>{H("filechild", "<x-fileinfo f=\"1\"><x-sub/></x-fileinfo>")}{cmt("in mxfile")}'
            f'<diagram name="Foreign" id="f1"{ua("diagram")}>{body}{diag_post}</diagram>'
            f'{H("filechild", "<x-filetail/>")}</mxfile>{H("comments", chr(10) + "<!-- trailing comment -->")}\n')


def foreign(emit):
    fam = "foreign"
    variants = {
        "unknown-attributes-everywhere": {"attrs"},
        "unknown-children-in-cells": {"cellchild"},
        "unknown-children-in-geometry": {"geochild"},
        "unknown-children-in-model-and-root": {"modelchild"},
        "unknown-children-in-diagram-and-mxfile": {"filechild"},
        "xml-comments": {"comments"},
        "processing-instructions": {"pi"},
        "cdata-in-unknown-elements": {"cdata"},
        "namespaced-attributes-and-elements": {"ns"},
        "mixed-content-in-unknown-elements": {"mixed"},
        "entities-in-unknown-elements": {"entities"},
        "everything-combined": {"attrs", "cellchild", "geochild", "modelchild", "filechild", "comments", "pi", "cdata", "ns", "mixed", "entities"},
    }
    for name, h in variants.items():
        emit(fam, name, _foreign_doc(h), sorted(h) + ["unknown-xml"])
    emit(fam, "everything-combined-compressed", _foreign_doc(variants["everything-combined"] - {"filechild"}, "deflate"),
         ["unknown-xml", "compressed", "comments-in-compressed", "cdata"])
    # unknown top-level siblings of diagram inside mxfile with a second known page
    p = Page("Known", seed=2200); _small(p, 2)
    emit(fam, "unknown-between-pages",
         mxfile([p, Page("Second", seed=2201)], extra_pre=['<x-before kind="pre"/>'], extra_post=['<x-after>tail</x-after>']),
         ["unknown-xml", "multi-page"])


# --------------------------------------------------------------------------- stress

def stress(emit):
    fam = "stress"
    for total, enc in ((1000, "plain"), (5000, "deflate")):
        rng = random.Random(total)
        p = Page(f"{total} cells", seed=total, enc=enc, ids="seq")
        side = int((total * 0.7) ** 0.5) + 1
        ids = []
        n_vertices = int(total * 0.7)
        for i in range(n_vertices):
            ids.append(p.v(f"N{i}", 20 + (i % side) * 110, 20 + (i // side) * 70, 90, 40, rng.choice([ROUND, BOX, "ellipse;whiteSpace=wrap;html=1;", "rhombus;whiteSpace=wrap;html=1;"])))
        while p.cell_count + 2 < total:
            a, b = rng.sample(range(len(ids)), 2)
            p.e(ids[a], ids[b], rng.choice(["", "", "e"]), "edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;")
        emit(fam, f"{total}-cells", mxfile([p]), [f"stress-{total}"] + (["compressed"] if enc == "deflate" else []))


# --------------------------------------------------------------------------- pathological-but-valid

def pathological(emit):
    fam = "pathological"
    # empty diagram variants
    emit(fam, "empty-diagram-element", mxfile([Page("Empty", seed=2300, enc="empty")]), ["empty-diagram"])
    emit(fam, "empty-diagram-whitespace-text", '<mxfile><diagram name="Blank" id="b1">\n  </diagram></mxfile>', ["empty-diagram", "whitespace"])
    emit(fam, "only-root-cells", mxfile([Page("Blank", seed=2301)]), ["only-root-cells"])
    emit(fam, "only-cell-zero", '<mxfile><diagram name="Zero" id="z"><mxGraphModel><root><mxCell id="0"/></root></mxGraphModel></diagram></mxfile>', ["only-root-cells", "no-default-layer"])
    emit(fam, "empty-compressed-blank-page", mxfile([Page("Blank", seed=2302, enc="deflate"), Page("Blank legacy", seed=2303, enc="legacy")]), ["only-root-cells", "compressed", "legacy-compressed"])
    # out of order
    p = Page("Order", seed=2304)
    p.cells = []
    child = '<mxCell id="child" value="child first" style="rounded=1;" vertex="1" parent="parent"><mxGeometry x="10" y="10" width="80" height="30" as="geometry"/></mxCell>'
    edge = '<mxCell id="edge" edge="1" parent="1" source="child" target="parent"><mxGeometry relative="1" as="geometry"/></mxCell>'
    parent = '<mxCell id="parent" value="parent last" style="swimlane;container=1;" vertex="1" parent="layer-late"><mxGeometry x="40" y="40" width="200" height="120" as="geometry"/></mxCell>'
    p.cells = [edge, child, parent]
    p.root_tail = ['<mxCell id="layer-late" value="Late layer" parent="0"/>']
    emit(fam, "cells-out-of-parent-order", mxfile([p]), ["child-before-parent", "edge-before-vertices", "layer-after-cells"])
    # odd ids
    p = Page("Ids", seed=2305)
    ids = ["a", "007", "7", "id-with-dash", "id.with.dots", "id:colon", "ünï-ID-日本", "x" * 300, "_underscore", "a b c", "0x1F", "-1", "\U0001F600"]
    ids_made = [p.v(f"id={i[:20]}", 20 + (n % 4) * 190, 20 + (n // 4) * 80, 170, 60, ROUND, id=i) for n, i in enumerate(ids)]
    for a, b in zip(ids_made, ids_made[1:]):
        p.e(a, b, "", "html=1;", id=f"e-{a[:10]}")
    emit(fam, "odd-ids", mxfile([p]), ["non-numeric-ids", "long-id", "unicode-id", "id-with-space"])
    # geometry numbers
    p = Page("Numbers", seed=2306)
    specs = [("-100", "-50", "80", "40"), ("0.5", ".5", "10.25", "1e2"), ("-0", "+5", "80.", "40"), ("123456789.123456789", "0.000001", "100", "100"),
             ("1e3", "-1e-3", "50", "50"), ("0", "0", "0", "0"), ("100", "100", "100000", "10"), ("-9999999", "9999999", "1", "1")]
    for i, (x, y, w, h) in enumerate(specs):
        p.raw(el("mxCell", {"id": f"n{i}", "value": f"{x},{y}", "style": "rounded=1;", "vertex": "1", "parent": "1"},
                 [el("mxGeometry", {"x": x, "y": y, "width": w, "height": h, "as": "geometry"})]))
    emit(fam, "odd-numeric-formats", mxfile([p]), ["negative-geometry", "float-geometry", "exponent-numbers", "zero-size"])
    # rotation/flip
    p = Page("Rotation", seed=2307)
    for i, st in enumerate(["rotation=45", "rotation=90", "rotation=180", "rotation=270", "rotation=-30", "rotation=405", "rotation=0.5", "flipH=1", "flipV=1", "flipH=1;flipV=1;rotation=33",
                            "rotatable=0;rotation=10", "shape=triangle;direction=south", "shape=triangle;direction=west;flipH=1", "horizontal=0", "rotation=360"]):
        p.v(st, 20 + (i % 5) * 160, 20 + (i // 5) * 130, 100, 60, f"shape=trapezoid;whiteSpace=wrap;html=1;{st};" if "triangle" not in st else f"whiteSpace=wrap;html=1;{st};")
    emit(fam, "rotation-and-flips", mxfile([p]), ["rotation", "flipH", "flipV", "direction"])
    # deep nesting
    p = Page("Deep", seed=2308, enc="deflate")
    parent = None
    for d in range(60):
        parent = p.v(f"d{d}", 5, 5, 1000 - d * 10, 1000 - d * 10, "container=1;collapsible=0;whiteSpace=wrap;html=1;fillColor=none;", parent=parent)
    emit(fam, "nesting-depth-60", mxfile([p]), ["nested-containers", "deep-nesting", "compressed"])
    # huge label
    p = Page("Huge", seed=2309)
    p.v("Lorem ipsum é " * 9000, 20, 20, 300, 300, "whiteSpace=wrap;html=1;")
    emit(fam, "huge-label-100kb", mxfile([p]), ["long-value"])
    # dangling references
    p = Page("Dangling refs", seed=2310)
    p.raw('<mxCell id="v1" value="orphan parent" vertex="1" parent="no-such-parent"><mxGeometry x="10" y="10" width="80" height="30" as="geometry"/></mxCell>')
    p.raw('<mxCell id="e1" edge="1" parent="1" source="no-such-source" target="v1"><mxGeometry relative="1" as="geometry"/></mxCell>')
    p.raw('<mxCell id="plain" value="neither vertex nor edge" parent="1"/>')
    p.raw('<mxCell id="nogeo" value="vertex without geometry" vertex="1" parent="1"/>')
    emit(fam, "dangling-references", mxfile([p]), ["missing-parent", "missing-terminal", "no-geometry", "neither-vertex-nor-edge"])
    # misc odd cell attributes
    p = Page("Odd cells", seed=2311)
    p.raw('<mxCell id="a" value="both" vertex="1" edge="1" parent="1"><mxGeometry x="0" y="0" width="50" height="50" as="geometry"/></mxCell>')
    p.raw('<mxCell id="b" value="vertex=0" vertex="0" parent="1"><mxGeometry x="100" y="0" width="50" height="50" as="geometry"/></mxCell>')
    p.raw('<mxCell id="c" value="" vertex="1" parent="1" collapsed="1" visible="1" connectable="1" unknown="u"><mxGeometry x="200" y="0" width="50" height="50" relative="0" as="geometry"/></mxCell>')
    p.raw('<mxCell id="d" value="geometry with no as" vertex="1" parent="1"><mxGeometry x="300" y="0" width="50" height="50"/></mxCell>')
    emit(fam, "odd-cell-attributes", mxfile([p]), ["vertex-and-edge", "geometry-without-as", "unknown-cell-attrs"])
    # edge cases in the mxCell value
    p = Page("Value attr", seed=2312)
    p.raw('<mxCell id="n" vertex="1" parent="1"><mxGeometry x="0" y="0" width="50" height="50" as="geometry"/></mxCell>')
    p.raw('<mxCell id="m" value="" style="" vertex="1" parent="1"><mxGeometry x="100" y="0" width="50" height="50" as="geometry"/></mxCell>')
    emit(fam, "absent-vs-empty-value", mxfile([p]), ["absent-value-attr", "empty-value-attr", "empty-style"])


# --------------------------------------------------------------------------- bare libraries (.xml)

def library(emit):
    fam = "library"
    p = Page("lib", seed=2400, ids="rand")
    for i, (w, h, st) in enumerate([(120, 60, ROUND), (80, 80, "ellipse;whiteSpace=wrap;html=1;"), (100, 100, "rhombus;whiteSpace=wrap;html=1;"), (120, 40, "text;html=1;align=center;")]):
        p.v(f"Shape {i}", 20 + i * 140, 20, w, h, st)
    emit(fam, "bare-model-shapes", p.model_xml(), ["bare-mxGraphModel", "library-like", "rand-ids"], ext="xml")
    p = Page("flow", seed=2401); _small(p, 5)
    emit(fam, "bare-model-flow", p.model_xml(), ["bare-mxGraphModel", "edges"], ext="xml")
    p = Page("objs", seed=2402)
    for i in range(3):
        p.v("%name%", 20 + i * 150, 20, 120, 60, ROUND, obj=("object", {"placeholders": "1", "name": f"obj{i}"}))
    emit(fam, "bare-model-objects", '<?xml version="1.0" encoding="UTF-8"?>\n' + p.model_xml() + "\n", ["bare-mxGraphModel", "object", "xml-declaration"], ext="xml")
    p = Page("minimal", seed=2403, model={})
    p.v("x", 0, 0, 10, 10, None)
    emit(fam, "bare-model-no-attrs", p.model_xml(), ["bare-mxGraphModel", "model-attrs", "no-style-attr"], ext="xml")


def generate(emit):
    for fn in (encodings, pages, layers, containers, edges, objects, labels, styles, foreign, stress, pathological, library):
        fn(emit)
