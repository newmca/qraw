#!/usr/bin/env python3
"""Count, per difference class, the sample files that use the class's feature and how far
Qraw is from draw.io on them (docs/FIDELITY.md quotes these numbers).

    python3 tools/fidelity/classify.py <out-dir>

Reads <out-dir>/results.csv, the uncompressed inputs in <out-dir>/plain/ and the export log
<out-dir>/qraw-export.log (Qraw's own "unknown shape" / "edge shape" notes); writes
<out-dir>/classes.md. A class is a feature test, not a diagnosis: "files using it" are the
files where the feature occurs, "over 10 %" those among them with a layout score above 10 %.
Files can belong to several classes, so the classes overlap.
"""
import csv
import os
import re
import sys
from collections import defaultdict

# (id, title, how the feature is detected)
# kind "xml": regex over the uncompressed XML (styles and labels); kind "log": regex over Qraw's log lines of the file.
CLASSES = [
    ("floorplan-wall", "Floor plan walls (JS-only shapes)", "xml", r"shape=mxgraph\.floorplan\.wall"),
    ("unknown-stencil", "Stencil sets that are not shipped (fallback painter)", "log", r"unknown shape (mxgraph\.(aws3d|bootstrap|archimate3|ios7ui|mockup|kubernetes|aws4\.resourceIcon)[\w.]*)"),
    ("js-basic-shapes", "Shapes.js shapes without a painter (or, xor, delay, display, sumEllipse, dataStorage)", "log", r"unknown shape (or|xor|delay|display|sumEllipse|dataStorage|manualInput|internalStorage|star)\b"),
    ("images", "Images not shipped (img/lib and other image= references)", "xml", r"image=(img/lib|https?:)|shape=image"),
    ("html-table", "HTML tables inside labels", "xml", r"&lt;table"),
    ("trailing-br", "A line break at the end of a block (p, div, li)", "xml", r"&lt;br&gt;&lt;/(p|div|li)&gt;"),
    ("shape-rect", "Shape names draw.io does not register, such as shape=rect (draw.io: plain rectangle)", "log", r"unknown shape rect\b"),
    ("sysml-uml-packages", "Package and frame stencils (mxgraph.sysml, umlFrame)", "xml", r"mxgraph\.sysml|shape=umlFrame"),
    ("top-labels", "Top- or bottom-aligned text inside shapes", "xml", r"verticalAlign=(top|bottom)"),
    ("label-background", "Label background and border colours", "xml", r"labelBackgroundColor=#|labelBorderColor="),
    ("html-blocks", "HTML lists and block alignment in labels", "xml", r"&lt;(ul|ol|li|div)\b"),
    ("sketch", "Sketch and comic rendering", "xml", r"sketch=1|comic=1"),
    ("fill-style", "Hatch and other fill styles", "xml", r"fillStyle=(?!solid)"),
    ("glass", "Glass effect", "xml", r"glass=1"),
    ("edge-shapes", "Edge shapes link, flexArrow, arrow", "log", r"edge shape (link|flexArrow|arrow)"),
    ("isometric", "isometricEdgeStyle", "xml", r"isometricEdgeStyle"),
    ("jumps", "Line jumps", "xml", r"jumpStyle="),
    ("shadow", "Shadows", "xml", r"shadow=1"),
    ("gradient", "Gradients", "xml", r"gradientColor="),
    ("rotation", "Rotated shapes and labels", "xml", r"rotation=-?[1-9]"),
    ("dashed", "Dashed strokes", "xml", r"dashed=1"),
    ("markers", "Arrow markers other than the default", "xml", r"(end|start)Arrow=(?!classic;)"),
    ("orthogonal", "Orthogonal and elbow edge routing", "xml", r"edgeStyle=(orthogonalEdgeStyle|elbowEdgeStyle)"),
    ("curved", "Curved edges", "xml", r"curved=1"),
    ("exit-entry", "Edge exit and entry constraints", "xml", r"(exit|entry)[XY]="),
    ("swimlanes", "Swimlanes and containers", "xml", r"swimlane|container=1|childLayout"),
    ("collapsed", "Collapsed cells", "xml", r'collapsed="1"'),
    ("unicode-rtl", "Non-Latin scripts and right-to-left text", "xml", r"[֐-ۿऀ-෿぀-鿿]|direction=rtl|textDirection=rtl"),
    ("rounded", "Rounded corners (arcSize)", "xml", r"rounded=1|arcSize="),
    ("vendor-icons", "AWS4, Azure, GCP, IBM and Cisco icon shapes", "xml", r"mxgraph\.(aws4|azure|gcp2|ibm|cisco|network|kubernetes)"),
]


def main() -> int:
    out = sys.argv[1]
    rows = {r["id"]: r for r in csv.DictReader(open(os.path.join(out, "results.csv"), encoding="utf-8"))}
    plain = {}
    for name in os.listdir(os.path.join(out, "plain")):
        plain[os.path.splitext(name)[0]] = open(os.path.join(out, "plain", name), encoding="utf-8", errors="replace").read()
    log = defaultdict(list)
    log_path = os.path.join(out, "qraw-export.log")
    if os.path.exists(log_path):
        for line in open(log_path, encoding="utf-8", errors="replace"):
            m = re.search(r"/inputs/([^:]+)\.(?:drawio|xml): \w+: (.*)", line)
            if m:
                log[m.group(1)].append(m.group(2))

    lines = ["| Class | files using it | over 10 % | mean layout score | mean pixel score | worst example |", "|---|---|---|---|---|---|"]
    members = {}
    for cid, title, kind, pattern in CLASSES:
        rx = re.compile(pattern)
        ids = [i for i in rows if (rx.search(plain.get(i, "")) if kind == "xml" else any(rx.search(l) for l in log.get(i, [])))]
        members[cid] = ids
        if not ids:
            lines.append(f"| {title} (`{cid}`) | 0 | 0 | | | |")
            continue
        layout = [float(rows[i]["combined"]) for i in ids]
        pixel = [float(rows[i]["pixel_combined"]) for i in ids]
        worst = max(ids, key=lambda i: float(rows[i]["combined"]))
        short = worst.replace("tests__corpus__", "").replace("upstream-templates__", "").replace("generated__", "").replace("tools__fidelity__probes__", "probe/")
        lines.append(f"| {title} (`{cid}`) | {len(ids)} | {sum(v > 0.10 for v in layout)} | {100 * sum(layout) / len(layout):.1f} % | "
                     f"{100 * sum(pixel) / len(pixel):.1f} % | {short} ({100 * max(layout):.0f} %) |")
    with open(os.path.join(out, "classes.md"), "w", encoding="utf-8") as f:
        f.write("# Difference classes\n\n" + "\n".join(lines) + "\n")
        f.write("\n## Members\n\n")
        for cid, ids in members.items():
            f.write(f"- `{cid}`: " + ", ".join(sorted(i.split("__")[-1] for i in ids)) + "\n")
    print(f"classes: {os.path.join(out, 'classes.md')}")
    return 0


sys.exit(main())
