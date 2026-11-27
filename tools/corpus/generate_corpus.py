#!/usr/bin/env python3
"""Generate the Qraw .drawio regression corpus.

    python3 -I tools/corpus/generate_corpus.py                       # (re)generate tests/corpus/generated + manifest + README
    python3 -I tools/corpus/generate_corpus.py --sync-upstream DIR   # also refresh tests/corpus/upstream-templates from a draw.io checkout

The output is deterministic: fixed seeds, no timestamps, sorted iteration. Only `zlib`
output can differ between zlib implementations; the decoded XML never does.
Stdlib only. The script validates its own output and exits non-zero on any problem.
"""

from __future__ import annotations

import argparse
import shutil
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # `python3 -I` drops the script dir from sys.path

import families_diagrams  # noqa: E402
import families_edgecases  # noqa: E402
from xmlkit import decompress_page  # noqa: E402

REPO = HERE.parents[1]
MIN_GENERATED = 110
MIN_TOTAL = 260
MAX_BYTES = 15 * 1024 * 1024
UPSTREAM_DIR = "upstream-templates"


# --------------------------------------------------------------------------- emit / write

class Emitter:
    def __init__(self) -> None:
        self.files: dict[str, tuple[bytes, list[str]]] = {}  # relpath under generated/ -> (bytes, tags)

    def __call__(self, family: str, name: str, content: str | bytes, tags: list[str], ext: str = "drawio") -> None:
        rel = f"{family}/{name}.{ext}"
        if rel in self.files:
            raise SystemExit(f"duplicate corpus file {rel}")
        data = content if isinstance(content, bytes) else content.encode("utf-8")
        self.files[rel] = (data, sorted(set(tags)))


def write_generated(out: Path, em: Emitter) -> None:
    gen = out / "generated"
    if gen.exists():
        shutil.rmtree(gen)  # owned exclusively by this script
    for rel, (data, _) in sorted(em.files.items()):
        path = gen / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


# --------------------------------------------------------------------------- analysis

def analyze(data: bytes) -> dict:
    """Parse a corpus file and every compressed page in it. Raises on any malformation."""
    root = ET.fromstring(data)
    info = {"pages": 0, "cells": 0, "objects": 0, "enc": Counter()}

    def count(model: ET.Element) -> None:
        if model.tag != "mxGraphModel":
            raise ValueError(f"page content is <{model.tag}>, expected mxGraphModel")
        info["cells"] += sum(1 for _ in model.iter("mxCell"))
        info["objects"] += sum(1 for e in model.iter() if e.tag in ("object", "UserObject"))

    if root.tag == "mxGraphModel":
        info["enc"]["bare-model"] += 1
        info["pages"] = 1
        count(root)
    elif root.tag == "mxfile":
        for d in root.findall("diagram"):
            info["pages"] += 1
            inline = d.find("mxGraphModel")
            text = (d.text or "").strip()
            if inline is not None:
                info["enc"]["plain"] += 1
                count(inline)
            elif not text:
                info["enc"]["empty"] += 1
            else:
                xml, kind = decompress_page(text)
                info["enc"][kind] += 1
                count(ET.fromstring(xml))
    else:
        raise ValueError(f"unexpected root <{root.tag}>")
    return info


def features_of(info: dict, tags: list[str]) -> str:
    auto = [f"pages={info['pages']}", f"cells={info['cells']}"]
    auto += [f"enc:{k}" for k in sorted(info["enc"])]
    if info["objects"]:
        auto.append(f"objects={info['objects']}")
    return ",".join(auto + tags)


# --------------------------------------------------------------------------- upstream templates

NOTICE = """# Upstream draw.io templates

Byte-exact copies of the diagram templates shipped with draw.io, used as real-world
regression input for the `.drawio` codec.

- Source: https://github.com/jgraph/drawio, branch `dev`{commit}
- Path in source: `src/main/webapp/templates/**/*.xml` (`index.xml` is a template list, not a diagram, and is not copied)
- draw.io VERSION file: `{version}`
- Project licence: Apache-2.0, see `LICENSE-APACHE-2.0.txt` (copy of the upstream `LICENSE`).
- The upstream `templates/` directory additionally carries its own `LICENSE`
  (Creative Commons Attribution 4.0 International). It is reproduced as
  `LICENSE-CC-BY-4.0.txt` because it is the licence file that sits next to these files upstream.

The files are not modified. Do not reformat them: the corpus tests rely on exact bytes.
Refresh with `python3 -I tools/corpus/generate_corpus.py --sync-upstream <drawio checkout>`.
"""


def sync_upstream(src: Path, out: Path, commit: str | None) -> None:
    templates = src / "src" / "main" / "webapp" / "templates"
    dest = out / UPSTREAM_DIR
    if dest.exists():
        shutil.rmtree(dest)
    n = 0
    for f in sorted(templates.rglob("*.xml")):
        if f.name == "index.xml" and f.parent == templates:
            continue
        target = dest / f.relative_to(templates)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(f, target)
        n += 1
    shutil.copyfile(src / "LICENSE", dest / "LICENSE-APACHE-2.0.txt")
    shutil.copyfile(templates / "LICENSE", dest / "LICENSE-CC-BY-4.0.txt")
    version = (src / "VERSION").read_text().strip()
    (dest / "NOTICE.md").write_text(NOTICE.format(version=version, commit=f" (commit `{commit}`)" if commit else ""))
    print(f"synced {n} upstream templates (VERSION {version})")


# --------------------------------------------------------------------------- manifest / README

README = """# Qraw `.drawio` regression corpus

Generated by `tools/corpus/generate_corpus.py`. Do not edit this file by hand: change the
template in the script and regenerate.

## Layout

| Path | Content |
|---|---|
| `upstream-templates/<category>/*.xml` | {n_up} byte-exact draw.io templates (Apache-2.0 / CC BY 4.0, see `upstream-templates/NOTICE.md`). Real-world files. |
| `generated/<family>/*.drawio` | {n_gen} synthetic files from the generator, one folder per family. {n_xml} are bare `<mxGraphModel>` `.xml` files in `generated/library/`. |
| `MANIFEST.tsv` | `path`, `family`, `features` for every corpus file (paths relative to `tests/corpus`). `features` starts with auto-detected `pages=`, `cells=`, `enc:` (plain, deflate, legacy, empty, bare-model) and `objects=` entries, followed by hand-written feature tags. |

Total: **{n_total} files, {size_mb:.1f} MB**.

Page encodings: `deflate` is `base64(deflateRaw(encodeURIComponent(xml)))`, `legacy` is
`base64(deflateRaw(xml))` without URI encoding (very old draw.io versions).

## Regenerate

```
python3 -I tools/corpus/generate_corpus.py
python3 -I tools/corpus/generate_corpus.py --sync-upstream /path/to/drawio   # only to refresh upstream-templates
```

The generator is stdlib-only and deterministic (fixed seeds, no timestamps); running it twice produces
byte-identical output on the same zlib implementation. It validates everything it writes: every file is
well-formed XML, every compressed page inflates to a well-formed `<mxGraphModel>`, and the file-count and
size budgets hold. It exits non-zero otherwise.

## Counts per family

| Family | Files | What it covers |
|---|---:|---|
{family_rows}

## Notes for consumers

- Files in `generated/pathological/` are valid XML but intentionally unusual (empty `<diagram/>`, only the
  root cells, missing parents/terminals, odd number formats). A lossless loader must accept them and
  round-trip them without dropping anything.
- `generated/foreign/` and `generated/encoding/` hold unknown elements/attributes, comments, processing
  instructions, CDATA, BOM, CRLF and single-quoted attributes. These stress the unknown-XML preservation rules.
- Expect `load -> save -> load` to be semantically equal and `load -> save -> load -> save` to be byte-stable for every file.
"""

FAMILY_BLURB = {
    "flowchart": "flowcharts, ports, loops, constraints, multi-page", "uml-class": "class, interface, package diagrams",
    "uml-sequence": "lifelines, free-floating messages, unicode actors", "uml-activity": "activity diagrams, fork/join",
    "uml-state": "state machines, self transitions", "bpmn": "pools, events, gateways, subprocess", "er-tables": "shape=table, crow's foot, entities",
    "network": "topologies, subnets, racks", "cloud": "AWS / Azure / GCP / Kubernetes icon styles", "orgchart": "trees, unicode names, placeholders",
    "mockup": "wireframes, sketch style, images", "floorplan": "walls, rotation, page scale", "swimlanes": "pools, lanes, stack layouts, collapsed",
    "whiteboard": "sketch shapes, emoji, mind maps", "encoding": "plain/compressed/legacy/wrapped pages, BOM, CRLF, quoting", "pages": "up to 12 pages, page and mxfile attributes",
    "layers": "multiple, hidden, locked layers", "containers": "groups, nested/collapsed containers", "edges": "waypoints, ports, constraints, edge-to-edge, markers",
    "objects": "object/UserObject wrappers, placeholders, links", "labels": "HTML, entities, Unicode, newlines, math", "styles": "bare tokens, empty values, data URIs, stencils, long styles",
    "foreign": "unknown attributes/elements, comments, PIs, CDATA, namespaces", "stress": "1000 and 5000 cells", "pathological": "valid-but-odd documents",
    "library": "bare mxGraphModel library-like files",
}


def build_manifest_and_readme(out: Path, em: Emitter, errors: list[str]) -> tuple[int, int]:
    rows: list[tuple[str, str, str]] = []
    fam_counts: Counter = Counter()
    n_up = 0
    total_bytes = 0
    up_root = out / UPSTREAM_DIR
    for f in sorted(up_root.rglob("*.xml")) if up_root.exists() else []:
        rel = f.relative_to(out).as_posix()
        data = f.read_bytes()
        total_bytes += len(data)
        try:
            rows.append((rel, f"upstream/{f.parent.relative_to(up_root).as_posix()}", features_of(analyze(data), ["upstream"])))
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{rel}: {exc}")
        n_up += 1
    n_xml = 0
    for rel, (data, tags) in sorted(em.files.items()):
        total_bytes += len(data)
        fam = rel.split("/", 1)[0]
        fam_counts[fam] += 1
        n_xml += rel.endswith(".xml")
        try:
            rows.append((f"generated/{rel}", fam, features_of(analyze(data), tags)))
        except Exception as exc:  # noqa: BLE001
            errors.append(f"generated/{rel}: {exc}")
    rows.sort()
    (out / "MANIFEST.tsv").write_text("path\tfamily\tfeatures\n" + "".join("\t".join(r) + "\n" for r in rows))

    family_rows = "\n".join(f"| {fam} | {fam_counts[fam]} | {FAMILY_BLURB.get(fam, '')} |" for fam in sorted(fam_counts))
    up_cats = Counter(r[1] for r in rows if r[1].startswith("upstream/"))
    if up_cats:
        family_rows += f"\n| upstream/* | {n_up} | " + ", ".join(f"{k.split('/', 1)[1]} {v}" for k, v in sorted(up_cats.items())) + " |"
    n_gen = sum(fam_counts.values())
    (out / "README.md").write_text(README.format(
        n_up=n_up, n_gen=n_gen, n_xml=n_xml, n_total=n_up + n_gen, size_mb=total_bytes / 2**20, family_rows=family_rows))
    return n_up + n_gen, total_bytes


# --------------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=REPO / "tests" / "corpus", help="corpus root (default: tests/corpus)")
    ap.add_argument("--sync-upstream", type=Path, metavar="DIR", help="draw.io checkout to copy templates from")
    ap.add_argument("--upstream-commit", help="commit hash to record in NOTICE.md")
    args = ap.parse_args()

    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    if args.sync_upstream:
        sync_upstream(args.sync_upstream, out, args.upstream_commit)

    em = Emitter()
    families_diagrams.generate(em)
    families_edgecases.generate(em)
    write_generated(out, em)

    errors: list[str] = []
    total, size = build_manifest_and_readme(out, em, errors)
    if len(em.files) < MIN_GENERATED:
        errors.append(f"only {len(em.files)} generated files (< {MIN_GENERATED})")
    if (out / UPSTREAM_DIR).exists() and total < MIN_TOTAL:
        errors.append(f"only {total} corpus files (< {MIN_TOTAL})")
    if size >= MAX_BYTES:
        errors.append(f"corpus is {size} bytes (>= {MAX_BYTES})")
    for e in errors:
        print("ERROR:", e, file=sys.stderr)
    print(f"{len(em.files)} generated, {total} total files, {size / 2**20:.2f} MB, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
