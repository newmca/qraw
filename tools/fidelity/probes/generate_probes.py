#!/usr/bin/env python3
"""Generate the small feature probes used by the fidelity run.

Each probe is a single-page .drawio file with a grid of cells that differ in one style
feature each, so a difference between draw.io and Qraw can be attributed to that
feature. The output is checked in next to this script; rerun it after editing:

    python3 tools/fidelity/probes/generate_probes.py
"""
import os
from xml.sax.saxutils import quoteattr

HERE = os.path.dirname(os.path.abspath(__file__))


class Page:
    def __init__(self, name):
        self.name, self.cells, self.next_id = name, [], 2

    def vertex(self, x, y, w, h, style, value=""):
        cid = f"c{self.next_id}"; self.next_id += 1
        self.cells.append(
            f'<mxCell id="{cid}" value={quoteattr(value)} style={quoteattr(style)} vertex="1" parent="1">'
            f'<mxGeometry x="{x}" y="{y}" width="{w}" height="{h}" as="geometry"/></mxCell>')
        return cid

    def edge(self, style, src=None, dst=None, points=(), a=None, b=None, value=""):
        cid = f"c{self.next_id}"; self.next_id += 1
        ends = (f' source="{src}"' if src else "") + (f' target="{dst}"' if dst else "")
        geo = ""
        if a:
            geo += f'<mxPoint x="{a[0]}" y="{a[1]}" as="sourcePoint"/>'
        if b:
            geo += f'<mxPoint x="{b[0]}" y="{b[1]}" as="targetPoint"/>'
        if points:
            geo += '<Array as="points">' + "".join(f'<mxPoint x="{x}" y="{y}"/>' for x, y in points) + "</Array>"
        self.cells.append(
            f'<mxCell id="{cid}" value={quoteattr(value)} style={quoteattr(style)} edge="1" parent="1"{ends}>'
            f'<mxGeometry relative="1" as="geometry">{geo}</mxGeometry></mxCell>')
        return cid

    def write(self):
        xml = (f'<mxfile host="fidelity-probe"><diagram name="{self.name}" id="{self.name}">'
               '<mxGraphModel dx="800" dy="600" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" '
               'arrows="1" fold="1" page="1" pageScale="1" pageWidth="827" pageHeight="1169" math="0" shadow="0">'
               '<root><mxCell id="0"/><mxCell id="1" parent="0"/>' + "".join(self.cells) +
               '</root></mxGraphModel></diagram></mxfile>\n')
        with open(os.path.join(HERE, f"{self.name}.drawio"), "w", encoding="utf-8") as f:
            f.write(xml)


def grid(page, items, cols, w=110, h=60, gx=30, gy=40, x0=20, y0=20, base=""):
    for i, (style, label) in enumerate(items):
        page.vertex(x0 + (i % cols) * (w + gx), y0 + (i // cols) * (h + gy), w, h, base + style, label)


def probe_fills():
    p = Page("fills-shadow-opacity")
    items = [
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;", "plain"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;shadow=1;", "shadow"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;rounded=1;shadow=1;", "round shadow"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;gradientColor=#7ea6e0;strokeColor=#6c8ebf;", "gradient south"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;gradientColor=#7ea6e0;gradientDirection=east;strokeColor=#6c8ebf;", "gradient east"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;gradientColor=#7ea6e0;gradientDirection=north;strokeColor=#6c8ebf;", "gradient north"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;gradientColor=#7ea6e0;gradientDirection=west;strokeColor=#6c8ebf;", "gradient west"),
        ("ellipse;whiteSpace=wrap;html=1;fillColor=#f8cecc;gradientColor=#ea6b66;strokeColor=#b85450;shadow=1;", "ellipse grad shadow"),
        ("whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;opacity=40;", "opacity 40"),
        ("whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;fillOpacity=30;strokeWidth=4;", "fillOpacity 30"),
        ("whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;strokeOpacity=30;strokeWidth=4;", "strokeOpacity 30"),
        ("whiteSpace=wrap;html=1;fillColor=#fff2cc;strokeColor=#d6b656;textOpacity=30;", "textOpacity 30"),
        ("whiteSpace=wrap;html=1;fillColor=#fff2cc;strokeColor=#d6b656;glass=1;rounded=1;", "glass"),
        ("whiteSpace=wrap;html=1;fillColor=none;strokeColor=#000000;strokeWidth=0;", "strokeWidth 0"),
        ("whiteSpace=wrap;html=1;fillColor=none;strokeColor=#000000;strokeWidth=1;", "strokeWidth 1"),
        ("whiteSpace=wrap;html=1;fillColor=none;strokeColor=#000000;strokeWidth=3;", "strokeWidth 3"),
        ("whiteSpace=wrap;html=1;fillColor=none;strokeColor=#000000;strokeWidth=8;", "strokeWidth 8"),
        ("whiteSpace=wrap;html=1;fillColor=none;strokeColor=#000000;strokeWidth=8;rounded=1;", "strokeWidth 8 round"),
        ("whiteSpace=wrap;html=1;fillColor=#ffffff;strokeColor=#000000;dashed=1;", "dashed"),
        ("whiteSpace=wrap;html=1;fillColor=#ffffff;strokeColor=#000000;dashed=1;dashPattern=1 4;strokeWidth=2;", "dotted 1 4"),
        ("whiteSpace=wrap;html=1;fillColor=#ffffff;strokeColor=#000000;dashed=1;dashPattern=8 8;strokeWidth=3;", "dash 8 8 w3"),
        ("whiteSpace=wrap;html=1;fillColor=#ffffff;strokeColor=#000000;dashed=1;fixDash=1;strokeWidth=3;", "fixDash"),
        ("rounded=1;whiteSpace=wrap;html=1;arcSize=50;fillColor=#e1d5e7;strokeColor=#9673a6;", "arcSize 50"),
        ("rounded=1;whiteSpace=wrap;html=1;arcSize=5;fillColor=#e1d5e7;strokeColor=#9673a6;", "arcSize 5"),
        ("rounded=1;whiteSpace=wrap;html=1;absoluteArcSize=1;arcSize=30;fillColor=#e1d5e7;strokeColor=#9673a6;", "absolute arc 30"),
        ("whiteSpace=wrap;html=1;fillColor=#000000;fontColor=#ffffff;", "black fill"),
        ("whiteSpace=wrap;html=1;fillColor=#ff0000;strokeColor=none;", "no stroke"),
        ("whiteSpace=wrap;html=1;fillColor=default;strokeColor=default;", "default colours"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;rotation=20;", "rotation 20"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;flipH=1;gradientColor=#7ea6e0;gradientDirection=east;", "flipH gradient"),
        ("whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;fillStyle=hatch;fillColor2=none;sketch=0;", "fillStyle hatch"),
    ]
    grid(p, items, 4)
    p.write()


def probe_shapes():
    p = Page("basic-shapes")
    names = ["rectangle", "ellipse", "rhombus", "triangle", "hexagon", "cylinder3", "cloud", "parallelogram",
             "trapezoid", "step", "document", "process", "card", "note", "folder", "actor", "cube", "tape",
             "callout", "doubleEllipse", "dataStorage", "manualInput", "internalStorage", "delay", "display",
             "or", "xor", "sumEllipse", "umlActor", "plus", "cross", "star", "mxgraph.basic.pie",
             "mxgraph.basic.donut", "mxgraph.basic.cloud_callout", "mxgraph.basic.heart", "mxgraph.basic.cylinder",
             "mxgraph.basic.4_point_star_2", "mxgraph.basic.octagon2", "mxgraph.basic.rounded_frame"]
    items = []
    for n in names:
        shape = "" if n == "rectangle" else f"shape={n};"
        items.append((f"{shape}whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;", n.split('.')[-1]))
    grid(p, items, 5, h=70)
    p.write()


def probe_labels():
    p = Page("labels")
    base = "whiteSpace=wrap;html=1;fillColor=#f5f5f5;strokeColor=#666666;"
    items = [
        (base + "fontSize=10;", "Helvetica 10 The quick brown fox jumps"),
        (base + "fontSize=12;", "Helvetica 12 The quick brown fox jumps"),
        (base + "fontSize=18;", "Helvetica 18 The quick brown fox"),
        (base + "fontFamily=Arial;fontSize=14;", "Arial 14 Wrapping happens here"),
        (base + "fontFamily=Times New Roman;fontSize=14;", "Times 14 Wrapping happens here"),
        (base + "fontFamily=Courier New;fontSize=14;", "Courier 14 Wrapping happens"),
        (base + "fontFamily=Verdana;fontSize=12;", "Verdana 12 Wrapping happens here"),
        (base + "fontFamily=Georgia;fontSize=12;", "Georgia 12 Wrapping happens here"),
        (base + "fontStyle=1;", "Bold"), (base + "fontStyle=2;", "Italic"), (base + "fontStyle=4;", "Underline"),
        (base + "fontStyle=3;", "Bold italic"), (base + "fontStyle=8;", "Strike through"),
        (base + "align=left;spacingLeft=8;", "align left"), (base + "align=right;spacingRight=8;", "align right"),
        (base + "verticalAlign=top;spacingTop=6;", "top"), (base + "verticalAlign=bottom;spacingBottom=6;", "bottom"),
        (base + "labelPosition=right;verticalLabelPosition=middle;align=left;", "label right"),
        (base + "labelPosition=left;verticalLabelPosition=middle;align=right;", "label left"),
        (base + "labelPosition=center;verticalLabelPosition=bottom;verticalAlign=top;", "label below"),
        (base + "labelPosition=center;verticalLabelPosition=top;verticalAlign=bottom;", "label above"),
        (base + "rotation=45;", "rotated 45"),
        (base + "horizontal=0;", "vertical text"),
        (base + "whiteSpace=nowrap;", "nowrap long label text that overflows the box"),
        (base, "<b>bold</b> <i>italic</i> <u>under</u> <font color=\"#ff0000\">red</font>"),
        (base, "line one<br>line two<br>line three"),
        (base, "<ul><li>first</li><li>second</li></ul>"),
        (base, "<div style=\"text-align: left;\">left para</div><div style=\"text-align: right;\">right para</div>"),
        (base, "<font style=\"font-size: 20px;\">big</font> small <sub>sub</sub> <sup>sup</sup>"),
        (base + "spacing=10;", "spacing 10 with a longer text"),
        (base + "labelBackgroundColor=#ffff00;labelBorderColor=#ff0000;", "label bg+border"),
        (base + "fontColor=#0000ff;", "blue text"),
        (base + "fontSize=8;", "Tiny 8 point text lines wrapped"),
        (base + "html=0;", "plain text label\nwith newline"),
        (base + "whiteSpace=wrap;overflow=hidden;", "overflow hidden with a very long text that does not fit in the shape at all"),
        (base + "overflow=fill;", "overflow fill text"),
        (base + "textDirection=rtl;", "rtl direction"),
        (base + "shape=ellipse;", "ellipse label wraps inside"),
        (base + "shape=rhombus;", "rhombus label"),
        (base + "shape=cylinder3;", "cylinder label"),
        (base + "shape=cloud;", "cloud label"),
    ]
    grid(p, items, 4, w=150, h=70, gx=30, gy=40)
    p.write()


EDGE_STYLES = [
    "", "edgeStyle=orthogonalEdgeStyle;", "edgeStyle=orthogonalEdgeStyle;rounded=1;", "edgeStyle=elbowEdgeStyle;elbow=horizontal;",
    "edgeStyle=elbowEdgeStyle;elbow=vertical;", "edgeStyle=entityRelationEdgeStyle;", "edgeStyle=isometricEdgeStyle;",
    "edgeStyle=orthogonalEdgeStyle;curved=1;", "curved=1;", "edgeStyle=segmentEdgeStyle;", "edgeStyle=orthogonalEdgeStyle;orthogonalLoop=1;",
    "shape=link;", "shape=flexArrow;", "shape=arrow;", "shape=filledEdge;", "dashed=1;", "strokeWidth=4;", "rounded=1;arcSize=20;",
]


def probe_edges():
    p = Page("edge-styles")
    box = "whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;"
    for i, es in enumerate(EDGE_STYLES):
        col, row = i % 3, i // 3
        x, y = 20 + col * 300, 20 + row * 130
        a = p.vertex(x, y, 60, 40, box, "A")
        b = p.vertex(x + 170, y + 60, 60, 40, box, "B")
        p.edge(es + "html=1;endArrow=classic;", a, b, value=(es.split(";")[0] or "straight")[:22])
    p.write()

    p = Page("edge-waypoints-ports")
    box = "whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;"
    a = p.vertex(20, 20, 80, 50, box, "A"); b = p.vertex(300, 20, 80, 50, box, "B")
    c = p.vertex(20, 200, 80, 50, box, "C"); d = p.vertex(300, 200, 80, 50, box, "D")
    p.edge("endArrow=classic;html=1;", a, b, points=[(160, 120), (240, 120)])
    p.edge("edgeStyle=orthogonalEdgeStyle;html=1;endArrow=classic;exitX=0.5;exitY=1;entryX=0.5;entryY=0;", a, c)
    p.edge("edgeStyle=orthogonalEdgeStyle;html=1;endArrow=classic;exitX=1;exitY=1;entryX=0;entryY=0;", b, d)
    p.edge("edgeStyle=orthogonalEdgeStyle;html=1;endArrow=classic;exitX=1;exitY=0.5;entryX=1;entryY=0.5;", c, d, points=[(200, 300), (420, 300)])
    p.edge("endArrow=classic;html=1;curved=1;", a, d, points=[(150, 140)])
    p.edge("endArrow=classic;html=1;labelBackgroundColor=#ffffff;", a=(450, 20), b=(560, 120), value="free edge")
    p.edge("edgeStyle=orthogonalEdgeStyle;html=1;endArrow=classic;loop=1;", c, c, points=[(60, 300)])
    p.edge("endArrow=classic;html=1;perimeter=0;", a=(450, 200), b=(560, 280), value="label")
    p.write()


def probe_markers():
    p = Page("arrow-markers")
    markers = ["classic", "classicThin", "block", "blockThin", "open", "openThin", "oval", "diamond", "diamondThin",
               "box", "halfCircle", "dash", "cross", "circlePlus", "circle", "ERone", "ERmandOne", "ERmany",
               "ERoneToMany", "ERzeroToOne", "ERzeroToMany", "baseDash", "doubleBlock", "async", "none"]
    for i, m in enumerate(markers):
        y = 20 + i * 36
        for j, (fill, w) in enumerate([(1, 1), (0, 1), (1, 3)]):
            x = 20 + j * 260
            p.edge(f"endArrow={m};endFill={fill};startArrow={m};startFill={fill};strokeWidth={w};html=1;",
                   a=(x + 80, y), b=(x + 220, y), value=f"{m} {w}" if j == 0 else "")
    p.write()
    p = Page("arrow-sizes")
    for i, sz in enumerate([2, 4, 6, 10, 14, 20]):
        y = 20 + i * 50
        p.edge(f"endArrow=classic;endSize={sz};startArrow=oval;startSize={sz};html=1;", a=(40, y), b=(240, y))
        p.edge(f"endArrow=block;endFill=0;endSize={sz};strokeWidth=3;html=1;", a=(300, y), b=(500, y))
        p.edge(f"endArrow=diamond;endSize={sz};strokeWidth=2;dashed=1;html=1;", a=(560, y), b=(760, y))
    p.write()


def probe_jumps():
    p = Page("line-jumps")
    for i, js in enumerate(["arc", "gap", "sharp", "line"]):
        y = 20 + i * 140
        p.edge(f"endArrow=none;html=1;jumpStyle={js};jumpSize=10;", a=(20, y + 60), b=(300, y + 60), value=js)
        for k in range(3):
            p.edge(f"endArrow=none;html=1;jumpStyle={js};jumpSize=10;", a=(80 + k * 80, y), b=(80 + k * 80, y + 120))
        p.edge(f"edgeStyle=orthogonalEdgeStyle;endArrow=none;html=1;jumpStyle={js};jumpSize=12;rounded=0;",
               a=(340, y + 20), b=(560, y + 100), points=[(450, y + 20), (450, y + 100)])
    p.write()


def probe_containers():
    p = Page("containers-swimlanes")
    p.vertex(20, 20, 300, 200, "swimlane;whiteSpace=wrap;html=1;startSize=30;fillColor=#dae8fc;strokeColor=#6c8ebf;", "swimlane")
    p.vertex(40, 70, 100, 40, "rounded=1;whiteSpace=wrap;html=1;", "child")
    p.vertex(380, 20, 300, 200, "swimlane;whiteSpace=wrap;html=1;startSize=30;horizontal=0;fillColor=#d5e8d4;strokeColor=#82b366;swimlaneFillColor=#ffffff;", "vertical lane")
    p.vertex(20, 260, 300, 40, "swimlane;whiteSpace=wrap;html=1;startSize=30;collapsible=1;collapsed=1;", "collapsed")
    p.vertex(380, 260, 300, 160, "swimlane;whiteSpace=wrap;html=1;startSize=30;rounded=1;swimlaneLine=0;fillColor=#fff2cc;strokeColor=#d6b656;shadow=1;", "rounded no line")
    p.vertex(20, 340, 300, 140, "swimlane;whiteSpace=wrap;html=1;startSize=30;childLayout=stackLayout;horizontal=1;horizontalStack=0;resizeParent=1;", "stack")
    p.vertex(20, 520, 300, 120, "shape=table;html=1;whiteSpace=wrap;startSize=0;container=1;collapsible=0;childLayout=tableLayout;", "")
    p.vertex(380, 460, 300, 180, "rounded=1;whiteSpace=wrap;html=1;container=1;verticalAlign=top;dashed=1;fillColor=none;", "container dashed")
    p.vertex(400, 510, 90, 40, "whiteSpace=wrap;html=1;", "in container")
    p.write()


def probe_sketch():
    p = Page("sketch-comic")
    base = "whiteSpace=wrap;html=1;"
    items = [(base + "sketch=1;curveFitting=1;jiggle=2;fillColor=#dae8fc;strokeColor=#6c8ebf;", "sketch rect"),
             (base + "sketch=1;ellipse;fillColor=#d5e8d4;strokeColor=#82b366;", "sketch ellipse"),
             (base + "sketch=1;shape=rhombus;fillColor=#fff2cc;strokeColor=#d6b656;", "sketch rhombus"),
             (base + "comic=1;fillColor=#f8cecc;strokeColor=#b85450;", "comic rect"),
             (base + "rounded=1;sketch=1;fillStyle=cross-hatch;fillColor=#e1d5e7;strokeColor=#9673a6;", "cross-hatch"),
             (base + "sketch=1;fillStyle=zigzag-line;fillColor=#dae8fc;strokeColor=#6c8ebf;", "zigzag")]
    grid(p, items, 3, h=70)
    a = p.vertex(20, 300, 60, 40, base + "sketch=1;", "A"); b = p.vertex(300, 300, 60, 40, base + "sketch=1;", "B")
    p.edge("endArrow=classic;html=1;sketch=1;curveFitting=1;jiggle=2;", a, b, value="sketch edge")
    p.write()


def main():
    for fn in (probe_fills, probe_shapes, probe_labels, probe_edges, probe_markers, probe_jumps, probe_containers, probe_sketch):
        fn()


main()
