"""Realistic diagram families: flowchart, UML, BPMN, ER, network, cloud, org chart,
mockup, floorplan, swimlanes, whiteboard.

Every generator receives `emit(family, name, text_or_bytes, tags)`.
"""

from __future__ import annotations

import random

from xmlkit import MXFILE_DESKTOP, MXFILE_OLD, MXFILE_WEB, Page, el, geo, mxfile, pt

# --------------------------------------------------------------------------- shared styles

BOX = "rounded=0;whiteSpace=wrap;html=1;"
ROUND = "rounded=1;whiteSpace=wrap;html=1;"
DECISION = "rhombus;whiteSpace=wrap;html=1;"
TERMINATOR = "ellipse;whiteSpace=wrap;html=1;"
ORTHO = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;"
PLAIN_EDGE = "endArrow=classic;html=1;rounded=0;"
CLASS_BOX = ("swimlane;fontStyle=1;align=center;verticalAlign=top;childLayout=stackLayout;horizontal=1;"
             "startSize=26;horizontalStack=0;resizeParent=1;resizeParentMax=0;resizeLast=0;collapsible=1;"
             "marginBottom=0;whiteSpace=wrap;html=1;")
CLASS_ROW = "text;strokeColor=none;fillColor=none;align=left;verticalAlign=top;spacingLeft=4;spacingRight=4;overflow=hidden;rotatable=0;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;whiteSpace=wrap;html=1;"
CLASS_LINE = "line;strokeWidth=1;fillColor=none;align=left;verticalAlign=middle;spacingTop=-1;spacingLeft=3;spacingRight=3;rotatable=0;labelPosition=right;points=[];portConstraint=eastwest;strokeColor=inherit;"

CLASS_NAMES = ["Order", "Customer", "Invoice", "Product", "Payment", "Shipment", "Address", "Cart",
               "Account", "Session", "User", "Role", "Permission", "Report", "Notification", "Catalog"]
FIELDS = ["id: int", "name: String", "createdAt: Date", "total: Decimal", "status: Status", "email: String",
          "items: List<Item>", "owner: User", "active: boolean", "version: long"]
METHODS = ["save(): void", "validate(): boolean", "toString(): String", "close()", "total(): Decimal",
           "find(id: int): Order", "notify(msg: String)", "equals(o: Object): boolean"]
PEOPLE = ["Ada Lovelace", "Grace Hopper", "Alan Turing", "李明", "Мария Иванова", "محمد علي", "דנה כהן",
          "José Álvarez", "Zoë O'Brien", "Søren Kierkegaard", "田中 太郎", "Nguyễn Văn An", "Åsa Lindqvist",
          "Chloé Dupont", "Łukasz Nowak", "Σοφία Παπά", "Priya Sharma", "Kofi Mensah", "Mateo García", "Aiko Sato"]
TITLES = ["CEO", "CTO", "CFO", "VP Engineering", "VP Sales", "Director", "Manager", "Lead", "Engineer", "Analyst"]


def _doc(pages, attrs=None, **kw):
    return mxfile(pages, attrs, **kw)


# --------------------------------------------------------------------------- flowchart

def _random_flow(p: Page, rng: random.Random, n: int, cols: int = 4) -> list[str]:
    kinds = [(ROUND, 120, 60), (DECISION, 120, 80), ("shape=parallelogram;perimeter=parallelogramPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60),
             ("shape=document;whiteSpace=wrap;html=1;boundedLbl=1;", 120, 70), (BOX, 120, 60),
             ("shape=cylinder3;whiteSpace=wrap;html=1;boundedLbl=1;backgroundOutline=1;size=15;", 80, 80)]
    ids = []
    for i in range(n):
        st, w, h = rng.choice(kinds)
        ids.append(p.v(f"Step {i + 1}", 40 + (i % cols) * 190, 40 + (i // cols) * 150, w, h, st))
    for i in range(1, n):
        p.e(ids[i - 1], ids[i], rng.choice(["", "", "yes", "no", "retry"]), ORTHO)
    for _ in range(max(1, n // 5)):
        a, b = rng.sample(range(n), 2)
        p.e(ids[a], ids[b], "", ORTHO + "dashed=1;")
    return ids


def flowcharts(emit):
    fam = "flowchart"
    # 1: basic, readable ids
    p = Page("Page-1", seed=1)
    s = p.v("Start", 340, 20, 120, 50, TERMINATOR)
    a = p.v("Read input", 340, 110, 120, 60, ROUND)
    d = p.v("Valid?", 340, 210, 120, 90, DECISION)
    ok = p.v("Process", 340, 340, 120, 60, ROUND)
    bad = p.v("Show error", 560, 225, 120, 60, ROUND)
    end = p.v("End", 340, 440, 120, 50, TERMINATOR)
    p.e(s, a, "", ORTHO); p.e(a, d, "", ORTHO)
    p.e(d, ok, "Yes", ORTHO, label_pos=(-0.2, 0))
    p.e(d, bad, "No", ORTHO)
    p.e(bad, a, "", ORTHO, points=[(620, 140)])
    p.e(ok, end, "", ORTHO)
    emit(fam, "basic-linear", _doc([p]), ["labels", "edge-waypoints"])

    # 2: same content, compressed page, random ids
    p = Page("Flow", seed=2, ids="rand", enc="deflate")
    _random_flow(p, random.Random(2), 18)
    emit(fam, "random-compressed", _doc([p], MXFILE_WEB), ["compressed", "rand-ids", "dashed-edges"])

    # 3: legacy compressed
    p = Page("Legacy flow", seed=3, enc="legacy")
    _random_flow(p, random.Random(3), 10, cols=3)
    emit(fam, "random-legacy-compressed", _doc([p], MXFILE_OLD), ["legacy-compressed"])

    # 4: shape catalog
    p = Page("Shapes", seed=4)
    shapes = ["shape=process;whiteSpace=wrap;html=1;backgroundOutline=1;", "shape=display;whiteSpace=wrap;html=1;",
              "shape=delay;whiteSpace=wrap;html=1;", "shape=manualInput;whiteSpace=wrap;html=1;size=20;",
              "shape=hexagon;perimeter=hexagonPerimeter2;whiteSpace=wrap;html=1;fixedSize=1;",
              "shape=cloud;whiteSpace=wrap;html=1;", "shape=trapezoid;perimeter=trapezoidPerimeter;whiteSpace=wrap;html=1;fixedSize=1;",
              "shape=step;perimeter=stepPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", "shape=tape;whiteSpace=wrap;html=1;",
              "shape=card;whiteSpace=wrap;html=1;", "shape=note;whiteSpace=wrap;html=1;backgroundOutline=1;darkOpacity=0.05;",
              "shape=internalStorage;whiteSpace=wrap;html=1;backgroundOutline=1;", "triangle;whiteSpace=wrap;html=1;",
              "shape=or;whiteSpace=wrap;html=1;", "shape=xor;whiteSpace=wrap;html=1;", "shape=cross;whiteSpace=wrap;html=1;",
              "shape=offPageConnector;whiteSpace=wrap;html=1;", "shape=curlyBracket;whiteSpace=wrap;html=1;rounded=1;flipH=1;labelPosition=right;verticalLabelPosition=middle;align=left;verticalAlign=middle;",
              "shape=sumEllipse;perimeter=ellipsePerimeter;whiteSpace=wrap;html=1;backgroundOutline=1;",
              "shape=storage;whiteSpace=wrap;html=1;"]
    for i, st in enumerate(shapes):
        p.v(st.split(";")[0].replace("shape=", ""), 30 + (i % 5) * 150, 30 + (i // 5) * 110, 110, 70, st)
    emit(fam, "shape-catalog", _doc([p]), ["many-shapes", "bare-token-styles"])

    # 5: dangling edges
    p = Page("Dangling", seed=5)
    a = p.v("A", 40, 40, 100, 50, ROUND); b = p.v("B", 300, 40, 100, 50, ROUND)
    p.e(a, None, "no target", PLAIN_EDGE, tp=(220, 200))
    p.e(None, b, "no source", PLAIN_EDGE, sp=(220, 220))
    p.e(None, None, "free", PLAIN_EDGE, sp=(40, 300), tp=(400, 340), points=[(200, 260)])
    p.e(a, b, "", "endArrow=none;html=1;")
    emit(fam, "dangling-edges", _doc([p]), ["dangling-edge", "sourcePoint", "targetPoint"])

    # 6: loops and constraints
    p = Page("Loop", seed=6)
    a = p.v("Init", 40, 40, 100, 50, ROUND); b = p.v("Work", 240, 40, 100, 50, ROUND)
    c = p.v("Check", 440, 30, 100, 70, DECISION); d = p.v("Done", 640, 40, 100, 50, TERMINATOR)
    p.e(a, b, "", ORTHO + "exitX=1;exitY=0.5;exitDx=0;exitDy=0;entryX=0;entryY=0.5;entryDx=0;entryDy=0;")
    p.e(b, c, "", ORTHO)
    p.e(c, b, "again", ORTHO + "exitX=0.5;exitY=1;exitDx=0;exitDy=0;entryX=0.5;entryY=1;entryDx=0;entryDy=0;", points=[(490, 160), (290, 160)])
    p.e(c, d, "ok", ORTHO)
    p.e(c, c, "self", ORTHO + "exitX=0.5;exitY=0;entryX=1;entryY=0.5;", points=[(490, -10), (590, -10), (590, 65)])
    emit(fam, "loops-constraints", _doc([p]), ["exit-entry-constraints", "self-loop", "edge-waypoints"])

    # 7: edge label offsets
    p = Page("Labels", seed=7)
    a = p.v("Alpha", 40, 100, 100, 50, BOX); b = p.v("Beta", 440, 100, 100, 50, BOX)
    for i, (x, off) in enumerate([(-0.8, (0, -15)), (-0.3, (10, 10)), (0, (0, 0)), (0.5, (-20, 25)), (0.9, (0, -30))]):
        e = p.e(a, b, f"lbl{i}", "endArrow=classic;html=1;", label_pos=(x, 0), offset=off)
    emit(fam, "edge-label-offsets", _doc([p]), ["relative-label", "offset"])

    # 8-9: bigger multi-page random flows (3 pages, mixed encodings)
    pages = [Page(f"Process {i + 1}", seed=80 + i, ids="rand", enc=["plain", "deflate", "legacy"][i]) for i in range(3)]
    for i, pg in enumerate(pages):
        _random_flow(pg, random.Random(80 + i), 12 + i * 6)
    emit(fam, "multipage-mixed-encodings", _doc(pages, MXFILE_DESKTOP), ["multi-page", "mixed-encodings"])

    p = Page("Big", seed=9, ids="rand", enc="deflate", pretty=False)
    _random_flow(p, random.Random(9), 120, cols=10)
    emit(fam, "large-120-nodes", _doc([p]), ["compressed", "many-edges"])

    # 10: ports (child connection points) + HTML labels
    p = Page("Ports", seed=10)
    a = p.v("Service A", 40, 60, 140, 80, "rounded=1;whiteSpace=wrap;html=1;container=1;collapsible=0;")
    pa = p.v("", 130, 30, 20, 20, "ellipse;whiteSpace=wrap;html=1;aspect=fixed;fillColor=#000000;connectable=1;port=1;", parent=a)
    b = p.v("Service B", 360, 60, 140, 80, "rounded=1;whiteSpace=wrap;html=1;container=1;collapsible=0;")
    pb = p.v("", -10, 30, 20, 20, "ellipse;whiteSpace=wrap;html=1;aspect=fixed;fillColor=#000000;", parent=b)
    p.e(pa, pb, "<b>port</b> to <i>port</i>", ORTHO)
    emit(fam, "edges-to-ports", _doc([p]), ["ports", "nested-cells", "html-label"])


# --------------------------------------------------------------------------- UML

def _class_box(p: Page, name: str, x, y, fields, methods, *, w=170, parent=None) -> str:
    h = 26 + 22 * len(fields) + 8 + 22 * len(methods)
    c = p.v(name, x, y, w, h, CLASS_BOX, parent=parent)
    yy = 26
    for f in fields:
        p.v(f"+ {f}", 0, yy, w, 22, CLASS_ROW, parent=c); yy += 22
    p.v("", 0, yy, w, 8, CLASS_LINE, parent=c); yy += 8
    for m in methods:
        p.v(f"+ {m}", 0, yy, w, 22, CLASS_ROW, parent=c); yy += 22
    return c


INHERIT = "endArrow=block;endSize=16;endFill=0;html=1;rounded=0;edgeStyle=orthogonalEdgeStyle;"
COMPOSE = "endArrow=diamondThin;endFill=1;endSize=24;html=1;rounded=0;"
AGGREGATE = "endArrow=diamondThin;endFill=0;endSize=24;html=1;rounded=0;"
ASSOC = "endArrow=open;endFill=1;html=1;rounded=0;"
DEPEND = "endArrow=open;endSize=12;dashed=1;html=1;rounded=0;"
REALIZE = "endArrow=block;dashed=1;endFill=0;endSize=12;html=1;rounded=0;"


def uml_class(emit):
    fam = "uml-class"
    for k, (title, enc, ids) in enumerate([("inheritance", "plain", "seq"), ("domain-model", "deflate", "rand"),
                                           ("associations", "plain", "rand")]):
        rng = random.Random(100 + k)
        p = Page(title, seed=100 + k, enc=enc, ids=ids)
        names = rng.sample(CLASS_NAMES, 6)
        boxes = []
        for i, n in enumerate(names):
            boxes.append(_class_box(p, n, 30 + (i % 3) * 240, 30 + (i // 3) * 260, rng.sample(FIELDS, 3), rng.sample(METHODS, 2)))
        styles = [INHERIT, COMPOSE, AGGREGATE, ASSOC, DEPEND, REALIZE]
        for i in range(1, 6):
            e = p.e(boxes[i], boxes[rng.randrange(i)], "", styles[i % len(styles)])
            p.edge_label(e, rng.choice(["1", "0..*", "1..*", "*"]), -0.8, 0, (0, -12))
            p.edge_label(e, rng.choice(["owns", "uses", "has"]), 0, 0, (0, 12))
        emit(fam, title, _doc([p]), ["swimlane-class", "edge-label-cells", "stackLayout"])

    # interfaces + lollipop
    p = Page("Interfaces", seed=110)
    i1 = p.v("<<interface>><br><b>Repository</b>", 40, 40, 160, 60, "html=1;whiteSpace=wrap;")
    c1 = p.v("SqlRepository", 40, 220, 160, 40, BOX)
    lol = p.v("", 330, 70, 20, 20, "ellipse;whiteSpace=wrap;html=1;fillColor=#fff2cc;strokeColor=#d6b656;")
    p.e(c1, i1, "", REALIZE)
    p.e(c1, lol, "", "endArrow=none;html=1;rounded=0;")
    emit(fam, "interfaces", _doc([p]), ["html-label", "realization"])

    # package diagram with nested containers
    p = Page("Packages", seed=111)
    outer = p.v("com.example", 20, 20, 520, 360, "shape=folder;fontStyle=1;spacingTop=10;tabWidth=40;tabHeight=14;tabPosition=left;html=1;whiteSpace=wrap;container=1;collapsible=0;verticalAlign=top;recursiveResize=0;")
    a = p.v("api", 20, 50, 200, 120, "shape=folder;tabWidth=40;tabHeight=14;html=1;whiteSpace=wrap;container=1;collapsible=0;verticalAlign=top;", parent=outer)
    b = p.v("impl", 280, 50, 200, 120, "shape=folder;tabWidth=40;tabHeight=14;html=1;whiteSpace=wrap;container=1;collapsible=0;verticalAlign=top;", parent=outer)
    p.v("Service", 20, 40, 80, 40, BOX, parent=a)
    p.v("ServiceImpl", 20, 40, 100, 40, BOX, parent=b)
    p.e(b, a, "&laquo;import&raquo;", DEPEND, parent=outer)
    emit(fam, "packages", _doc([p]), ["nested-containers", "edge-in-container"])

    # object diagram with underlined instance names (html fontStyle)
    p = Page("Objects", seed=112)
    for i in range(4):
        p.v(f"<u>order{i}:Order</u>", 40 + i * 170, 60, 140, 60, "html=1;whiteSpace=wrap;align=center;")
    emit(fam, "object-diagram", _doc([p]), ["html-label"])


def uml_sequence(emit):
    fam = "uml-sequence"
    LIFE = ("shape=umlLifeline;perimeter=lifelinePerimeter;whiteSpace=wrap;html=1;container=1;dropTarget=0;"
            "collapsible=0;recursiveResize=0;outlineConnect=0;portConstraint=eastwest;"
            'newEdgeStyle={"curved":0,"rounded":0};')
    ACT = ('html=1;points=[];perimeter=orthogonalPerimeter;outlineConnect=0;targetShapes=umlLifeline;'
           'portConstraint=eastwest;newEdgeStyle={"edgeStyle":"elbowEdgeStyle","elbow":"vertical","curved":0,"rounded":0};')
    scenarios = {
        "login": (["Browser", "Server", "AuthService", "Database"],
                  [(0, 1, "POST /login"), (1, 2, "authenticate(user, pw)"), (2, 3, "SELECT user"), (3, 2, "row"), (2, 1, "token"), (1, 0, "200 OK")]),
        "checkout": (["Client", "Cart", "Payment", "Inventory", "Mailer"],
                     [(0, 1, "checkout()"), (1, 3, "reserve(items)"), (3, 1, "ok"), (1, 2, "charge(total)"), (2, 1, "receipt"), (1, 4, "sendReceipt"), (1, 0, "confirmation")]),
        "async-compressed": (["A", "B", "C"], [(0, 1, "m1"), (1, 2, "m2"), (2, 0, "m3"), (0, 0, "self call")]),
        "unicode-actors": (["クライアント", "サーバー", "عميل", "שרת"], [(0, 1, "要求 → 応答"), (2, 3, "طلب"), (3, 2, "תשובה")]),
        "frames": (["User", "System"], [(0, 1, "request"), (1, 0, "response")]),
    }
    for k, (name, (actors, msgs)) in enumerate(scenarios.items()):
        p = Page(name, seed=200 + k, enc="deflate" if "compressed" in name else "plain", ids="rand" if k % 2 else "seq")
        life = []
        for i, a in enumerate(actors):
            life.append(p.v(a, 40 + i * 170, 40, 100, 400, LIFE))
        y = 120
        for (s, t, label) in msgs:
            xs, xt = 90 + s * 170, 90 + t * 170
            if s == t:
                act = p.v("", 45, y - 40, 10, 50, ACT, parent=life[s])
                p.e(None, None, label, "html=1;align=left;spacingLeft=2;endArrow=block;rounded=0;edgeStyle=orthogonalEdgeStyle;",
                    sp=(xs + 5, y - 20), tp=(xs + 5, y), points=[(xs + 60, y - 20), (xs + 60, y)])
            else:
                dashed = "dashed=1;" if label in ("ok", "row", "200 OK", "receipt") else ""
                p.e(None, None, label, f"html=1;verticalAlign=bottom;endArrow=block;curved=0;rounded=0;{dashed}", sp=(xs, y), tp=(xt, y))
            y += 55
        if name == "frames":
            p.v("alt", 20, 100, 360, 160, "shape=umlFrame;whiteSpace=wrap;html=1;pointerEvents=0;recursiveResize=0;container=1;collapsible=0;width=60;height=25;")
        emit(fam, name, _doc([p]), ["lifeline", "free-edge-points", "json-in-style"] + (["unicode"] if "unicode" in name else []))


def uml_activity(emit):
    fam = "uml-activity"
    for k, name in enumerate(["order-handling", "with-fork-join", "partitions", "object-flow"]):
        p = Page(name, seed=300 + k, enc="deflate" if k == 1 else "plain")
        init = p.v("", 200, 20, 30, 30, "ellipse;html=1;shape=startState;fillColor=#000000;strokeColor=#ff0000;")
        prev = init
        y = 90
        for lbl in ["Receive order", "Check stock", "Ship"]:
            n = p.v(lbl, 150, y, 130, 40, "rounded=1;whiteSpace=wrap;html=1;arcSize=40;")
            p.e(prev, n, "", ORTHO + "endArrow=open;"); prev = n; y += 80
        if name == "with-fork-join":
            fork = p.v("", 100, y, 230, 8, "html=1;points=[];perimeter=orthogonalPerimeter;fillColor=#000000;strokeColor=none;")
            p.e(prev, fork, "", ORTHO)
            a = p.v("Pack", 90, y + 50, 100, 40, "rounded=1;whiteSpace=wrap;html=1;arcSize=40;")
            b = p.v("Invoice", 240, y + 50, 100, 40, "rounded=1;whiteSpace=wrap;html=1;arcSize=40;")
            join = p.v("", 100, y + 120, 230, 8, "html=1;points=[];perimeter=orthogonalPerimeter;fillColor=#000000;strokeColor=none;")
            for src, dst in ((fork, a), (fork, b), (a, join), (b, join)):
                p.e(src, dst, "", ORTHO)
            prev, y = join, y + 150
        if name == "partitions":
            p.v("Customer", 20, 10, 80, 400, "swimlane;horizontal=0;whiteSpace=wrap;html=1;startSize=23;", id="lane-a")
        if name == "object-flow":
            obj = p.v("order:Order", 330, 90, 100, 40, "html=1;whiteSpace=wrap;align=center;")
            p.e(prev, obj, "", "endArrow=open;html=1;dashed=1;")
        d = p.v("", 195, y, 40, 40, DECISION)
        p.e(prev, d, "", ORTHO)
        fin = p.v("", 200, y + 80, 30, 30, "ellipse;html=1;shape=endState;fillColor=#000000;strokeColor=#ff0000;")
        p.e(d, fin, "[done]", ORTHO)
        emit(fam, name, _doc([p]), ["startState", "endState", "fork-bar"])


def uml_state(emit):
    fam = "uml-state"
    ST = "rounded=1;whiteSpace=wrap;html=1;arcSize=30;"
    for k, name in enumerate(["traffic-light", "document-lifecycle", "composite-state", "connection-fsm"]):
        p = Page(name, seed=400 + k, enc="legacy" if k == 3 else "plain", ids="rand" if k == 1 else "seq")
        states = {"traffic-light": ["Red", "Green", "Yellow"], "document-lifecycle": ["Draft", "Review", "Approved", "Published", "Archived"],
                  "composite-state": ["Idle", "Active", "Error"], "connection-fsm": ["Closed", "Listen", "SynRcvd", "Established", "FinWait"]}[name]
        ids = [p.v(s, 60 + i * 180, 120, 110, 50, ST) for i, s in enumerate(states)]
        init = p.v("", 10, 60, 20, 20, "ellipse;html=1;shape=startState;fillColor=#000000;strokeColor=#ff0000;")
        p.e(init, ids[0], "", ORTHO)
        for i in range(len(ids)):
            nxt = ids[(i + 1) % len(ids)]
            p.e(ids[i], nxt, f"e{i}", ORTHO, points=[] if i < len(ids) - 1 else [(115 + i * 180, 260), (115, 260)])
        p.e(ids[0], ids[0], "timeout", ORTHO + "exitX=0.25;exitY=0;entryX=0.75;entryY=0;", points=[(87, 80), (143, 80)])
        if name == "composite-state":
            comp = p.v("Active", 40, 300, 420, 160, "swimlane;whiteSpace=wrap;html=1;rounded=1;startSize=26;container=1;collapsible=0;")
            p.v("Running", 20, 50, 100, 40, ST, parent=comp); p.v("Paused", 200, 50, 100, 40, ST, parent=comp)
            p.v("H", 330, 55, 30, 30, "ellipse;whiteSpace=wrap;html=1;fontStyle=1;", parent=comp)
        emit(fam, name, _doc([p]), ["self-transition", "startState"])


# --------------------------------------------------------------------------- BPMN

def bpmn(emit):
    fam = "bpmn"
    EVT = "points=[[0.145,0.145,0],[0.5,0,0],[0.855,0.145,0],[1,0.5,0],[0.855,0.855,0],[0.5,1,0],[0.145,0.855,0],[0,0.5,0]];shape=mxgraph.bpmn.event;html=1;verticalLabelPosition=bottom;labelBackgroundColor=#ffffff;verticalAlign=top;align=center;perimeter=ellipsePerimeter;outlineConnect=0;aspect=fixed;outline={};symbol={};"
    TASK = "rounded=1;whiteSpace=wrap;html=1;arcSize=10;"
    GW = "shape=mxgraph.bpmn.gateway2;html=1;verticalLabelPosition=bottom;labelBackgroundColor=#ffffff;verticalAlign=top;align=center;perimeter=rhombusPerimeter;background=gateway;outlineConnect=0;outline=none;symbol=none;gwType={};"
    for k, name in enumerate(["order-process", "approval-with-gateways", "collaboration-two-pools", "event-types", "data-objects", "subprocess"]):
        p = Page(name, seed=500 + k, enc="deflate" if k == 2 else "plain", ids="rand" if k % 3 == 1 else "seq")
        pool_h = 220
        pool = p.v("Company", 20, 20, 820, pool_h, "swimlane;html=1;horizontal=0;startSize=20;whiteSpace=wrap;collapsible=0;")
        start = p.v("", 40, 90, 30, 30, EVT.format("standard", "general"), parent=pool)
        prev = start
        x = 100
        steps = ["Receive order", "Check credit", "Approve?", "Ship", "Invoice"]
        for i, s in enumerate(steps):
            if s.endswith("?"):
                n = p.v(s, x + 20, 85, 40, 40, GW.format("exclusive"), parent=pool)
            else:
                n = p.v(s, x, 70, 100, 70, TASK, parent=pool)
            p.e(prev, n, "", "edgeStyle=elbowEdgeStyle;fontSize=12;html=1;endArrow=blockThin;endFill=1;rounded=0;", parent=pool)
            prev = n; x += 130
        end = p.v("", x, 90, 30, 30, EVT.format("end", "terminate"), parent=pool)
        p.e(prev, end, "", "edgeStyle=elbowEdgeStyle;html=1;endArrow=blockThin;endFill=1;", parent=pool)
        if name == "collaboration-two-pools":
            pool2 = p.v("Customer", 20, 280, 820, 140, "swimlane;html=1;horizontal=0;startSize=20;whiteSpace=wrap;collapsible=0;")
            t = p.v("Place order", 100, 30, 100, 70, TASK, parent=pool2)
            p.e(t, start, "order", "edgeStyle=elbowEdgeStyle;html=1;dashed=1;dashPattern=8 4;endArrow=open;startArrow=oval;startFill=0;")
        if name == "event-types":
            for i, sym in enumerate(["message", "timer", "escalation", "error", "signal", "conditional", "link", "compensation"]):
                p.v(sym, 40 + i * 90, 260, 40, 40, EVT.format("throwing", sym))
        if name == "data-objects":
            p.v("Order", 100, 260, 36, 50, "shape=mxgraph.bpmn.data2;labelPosition=center;verticalLabelPosition=bottom;align=center;verticalAlign=top;html=1;size=14;")
            p.v("DB", 200, 260, 40, 50, "shape=datastore;whiteSpace=wrap;html=1;")
        if name == "subprocess":
            sp = p.v("Collapsed subprocess", 100, 260, 160, 80, "rounded=1;whiteSpace=wrap;html=1;arcSize=10;container=1;collapsible=1;collapsed=1;",
                     geo_children=[el("mxRectangle", {"x": 100, "y": 260, "width": 300, "height": 200, "as": "alternateBounds"})])
            p.v("inner", 20, 40, 80, 40, TASK, parent=sp)
        emit(fam, name, _doc([p]), ["pool", "mxgraph.bpmn", "points-in-style"])


# --------------------------------------------------------------------------- ER / tables

TABLE = "shape=table;startSize=30;container=1;collapsible=1;childLayout=tableLayout;fixedRows=1;rowLines=0;fontStyle=1;align=center;resizeLast=1;html=1;"
ROW = "shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;fillColor=none;collapsible=0;dropTarget=0;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;top=0;left=0;right=0;bottom=0;"
CELL_KEY = "shape=partialRectangle;connectable=0;fillColor=none;top=0;left=0;bottom=0;right=0;fontStyle=1;overflow=hidden;whiteSpace=wrap;html=1;"
CELL_VAL = "shape=partialRectangle;connectable=0;fillColor=none;top=0;left=0;bottom=0;right=0;align=left;spacingLeft=6;overflow=hidden;whiteSpace=wrap;html=1;"


def _table(p: Page, title: str, x, y, cols, *, w=180, with_alt=False) -> tuple[str, list[str]]:
    """cols: [(key, name)]. Returns (table id, row ids)."""
    t = p.v(title, x, y, w, 30 + 30 * len(cols), TABLE,
            geo_children=[el("mxRectangle", {"x": x, "y": y, "width": w, "height": 30, "as": "alternateBounds"})] if with_alt else ())
    rows = []
    for i, (key, name) in enumerate(cols):
        r = p.v("", 0, 30 + 30 * i, w, 30, ROW, parent=t)
        p.v(key, 0, 0, 30, 30, CELL_KEY, parent=r, geo_children=[el("mxRectangle", {"width": 30, "height": 30, "as": "alternateBounds"})])
        p.v(name, 30, 0, w - 30, 30, CELL_VAL, parent=r, geo_children=[el("mxRectangle", {"width": w - 30, "height": 30, "as": "alternateBounds"})])
        rows.append(r)
    return t, rows


def er_tables(emit):
    fam = "er-tables"
    CROW = "edgeStyle=entityRelationEdgeStyle;fontSize=12;html=1;endArrow=ERmany;startArrow=ERmandOne;endFill=0;startFill=0;"
    schema = {"customer": [("PK", "id"), ("", "name"), ("", "email")], "order": [("PK", "id"), ("FK", "customer_id"), ("", "placed_at"), ("", "total")],
              "order_line": [("PK", "id"), ("FK", "order_id"), ("FK", "product_id"), ("", "qty")], "product": [("PK", "id"), ("", "sku"), ("", "price")]}
    p = Page("Schema", seed=600)
    tabs = {n: _table(p, n, 40 + (i % 2) * 330, 40 + (i // 2) * 200, c) for i, (n, c) in enumerate(schema.items())}
    p.e(tabs["order"][1][1], tabs["customer"][1][0], "", CROW)
    p.e(tabs["order_line"][1][1], tabs["order"][1][0], "", CROW)
    p.e(tabs["order_line"][1][2], tabs["product"][1][0], "", CROW)
    emit(fam, "shop-schema-tables", _doc([p]), ["shape=table", "tableRow", "partialRectangle", "row-edges"])

    p = Page("Schema compressed", seed=601, enc="deflate", ids="rand")
    for i in range(12):
        _table(p, f"table_{i}", 30 + (i % 4) * 230, 30 + (i // 4) * 220, [("PK", "id")] + [("", f"col_{j}") for j in range(i % 4 + 1)], w=200, with_alt=(i % 3 == 0))
    emit(fam, "twelve-tables-alt-bounds", _doc([p]), ["compressed", "alternateBounds", "rand-ids"])

    # classic swimlane entity
    p = Page("Entities", seed=602)
    ENT = "swimlane;fontStyle=0;childLayout=stackLayout;horizontal=1;startSize=26;fillColor=none;horizontalStack=0;resizeParent=1;resizeParentMax=0;resizeLast=0;collapsible=1;marginBottom=0;html=1;"
    ents = []
    for i, n in enumerate(["Author", "Book", "Publisher"]):
        e = p.v(n, 40 + i * 240, 60, 160, 26 + 26 * 3, ENT)
        for j, f in enumerate(["id", "title", "isbn"]):
            p.v(f, 0, 26 + 26 * j, 160, 26, "text;strokeColor=none;fillColor=none;align=left;verticalAlign=top;spacingLeft=4;spacingRight=4;overflow=hidden;rotatable=0;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;html=1;", parent=e)
        ents.append(e)
    p.e(ents[0], ents[1], "writes", "edgeStyle=entityRelationEdgeStyle;fontSize=12;html=1;endArrow=ERzeroToMany;endFill=1;startArrow=ERmandOne;startFill=0;")
    p.e(ents[2], ents[1], "publishes", "edgeStyle=entityRelationEdgeStyle;fontSize=12;html=1;endArrow=ERoneToMany;startArrow=ERmandOne;")
    emit(fam, "stacked-entities", _doc([p]), ["stackLayout", "crows-foot"])

    # Chen notation
    p = Page("Chen", seed=603)
    e1 = p.v("Student", 40, 100, 100, 50, "whiteSpace=wrap;html=1;align=center;")
    e2 = p.v("Course", 380, 100, 100, 50, "whiteSpace=wrap;html=1;align=center;")
    rel = p.v("enrolls", 220, 95, 100, 60, "shape=rhombus;perimeter=rhombusPerimeter;whiteSpace=wrap;html=1;align=center;")
    at = p.v("<u>sid</u>", 40, 10, 80, 40, "ellipse;whiteSpace=wrap;html=1;align=center;")
    for a, b in ((e1, rel), (rel, e2), (at, e1)):
        p.e(a, b, "", "endArrow=none;html=1;rounded=0;")
    emit(fam, "chen-notation", _doc([p]), ["html-label"])

    # html table inside a label
    p = Page("HTML table", seed=604)
    html = ('<table border="1" width="100%" cellpadding="4" style="width:100%;height:100%;border-collapse:collapse;">'
            '<tr><th>Id</th><th>Name</th></tr><tr><td>1</td><td>A &amp; B</td></tr><tr><td>2</td><td>&lt;none&gt;</td></tr></table>')
    p.v(html, 40, 40, 200, 100, "verticalAlign=top;align=left;overflow=fill;html=1;whiteSpace=wrap;")
    emit(fam, "html-table-label", _doc([p]), ["html-label", "nested-markup"])

    # table with stacked row cells (merged-looking) and empty cells
    p = Page("Sparse", seed=605)
    _table(p, "sparse", 40, 40, [("", ""), ("", "only value"), ("K", ""), ("", "")], w=240)
    emit(fam, "sparse-table", _doc([p]), ["empty-values", "shape=table"])

    # table with child edges between row cells
    p = Page("Self ref", seed=606)
    t, rows = _table(p, "employee", 60, 60, [("PK", "id"), ("FK", "manager_id"), ("", "name")], w=220)
    p.e(rows[1], rows[0], "manages", "edgeStyle=orthogonalEdgeStyle;fontSize=12;html=1;endArrow=ERmany;startArrow=ERmandOne;", points=[(320, 135), (320, 105)])
    emit(fam, "self-referencing", _doc([p]), ["row-edges", "waypoints"])


# --------------------------------------------------------------------------- network

def network(emit):
    fam = "network"
    ROUTER = "shape=mxgraph.cisco.routers.router;html=1;pointerEvents=1;dashed=0;fillColor=#036897;strokeColor=#ffffff;strokeWidth=2;verticalLabelPosition=bottom;verticalAlign=top;align=center;outlineConnect=0;"
    SWITCH = "shape=mxgraph.cisco.switches.workgroup_switch;html=1;pointerEvents=1;dashed=0;fillColor=#036897;strokeColor=#ffffff;strokeWidth=2;verticalLabelPosition=bottom;verticalAlign=top;align=center;outlineConnect=0;"
    PC = "image;html=1;image=img/lib/clip_art/computers/Monitor_Tower_128x128.png;"
    SERVER = "shape=mxgraph.rack.general.1u_rack_server;html=1;labelPosition=right;align=left;spacingLeft=15;dashed=0;shadow=0;fillColor=#ffffff;"
    FW = "shape=mxgraph.cisco.security.firewall;html=1;pointerEvents=1;dashed=0;fillColor=#036897;strokeColor=#ffffff;strokeWidth=2;verticalLabelPosition=bottom;verticalAlign=top;align=center;"
    link = "endArrow=none;html=1;strokeWidth=2;"
    # star
    p = Page("Star", seed=700)
    core = p.v("core-sw", 340, 220, 78, 40, SWITCH)
    for i in range(8):
        import math
        ang = i * math.pi / 4
        n = p.v(f"host{i}", 340 + 220 * math.cos(ang), 230 + 180 * math.sin(ang), 60, 60, PC)
        p.e(core, n, "", link)
    emit(fam, "star-topology", _doc([p]), ["image=relative-path", "mxgraph.cisco"])
    # mesh, compressed
    p = Page("Mesh", seed=701, enc="deflate", ids="rand")
    rs = [p.v(f"R{i}", 60 + (i % 3) * 220, 60 + (i // 3) * 200, 78, 53, ROUTER) for i in range(6)]
    for i in range(6):
        for j in range(i + 1, 6):
            p.e(rs[i], rs[j], "", link + "dashed=1;" if (i + j) % 3 == 0 else link)
    emit(fam, "mesh-compressed", _doc([p]), ["compressed", "many-edges"])
    # datacenter with subnets
    p = Page("DC", seed=702)
    dmz = p.v("DMZ 10.0.1.0/24", 20, 20, 360, 220, "rounded=1;whiteSpace=wrap;html=1;dashed=1;fillColor=#fff2cc;strokeColor=#d6b656;container=1;collapsible=0;verticalAlign=top;align=left;spacingLeft=8;")
    lan = p.v("LAN 10.0.2.0/24", 420, 20, 360, 220, "rounded=1;whiteSpace=wrap;html=1;dashed=1;fillColor=#d5e8d4;strokeColor=#82b366;container=1;collapsible=0;verticalAlign=top;align=left;spacingLeft=8;")
    fw = p.v("fw", 380, 260, 60, 60, FW)
    web = p.v("web-01", 40, 60, 160, 15, SERVER, parent=dmz); web2 = p.v("web-02", 40, 100, 160, 15, SERVER, parent=dmz)
    db = p.v("db-01", 40, 60, 160, 15, SERVER, parent=lan)
    p.e(web, fw, "", link); p.e(db, fw, "", link); p.e(web2, fw, "", link)
    emit(fam, "datacenter-subnets", _doc([p]), ["containers", "mxgraph.rack"])
    # WAN
    p = Page("WAN", seed=703)
    inet = p.v("Internet", 300, 20, 160, 90, "ellipse;shape=cloud;whiteSpace=wrap;html=1;")
    sites = [p.v(f"Branch {c}", 40 + i * 230, 260, 78, 53, ROUTER) for i, c in enumerate("ABC")]
    for s in sites:
        p.e(inet, s, "VPN", "endArrow=none;html=1;strokeWidth=2;dashed=1;strokeColor=#CC0000;")
    emit(fam, "wan-vpn", _doc([p]), ["cloud-shape", "dashed-edges"])
    # VLAN
    p = Page("VLANs", seed=704)
    sw = p.v("sw1", 300, 40, 78, 40, SWITCH)
    for v, col in enumerate(["#dae8fc", "#f8cecc", "#e1d5e7"]):
        c = p.v(f"VLAN {10 * (v + 1)}", 40 + v * 250, 200, 220, 140, f"rounded=0;whiteSpace=wrap;html=1;fillColor={col};container=1;collapsible=0;verticalAlign=top;")
        for h in range(2):
            n = p.v(f"pc{v}{h}", 20 + h * 100, 50, 60, 60, PC, parent=c)
        p.e(sw, c, "trunk", link)
    emit(fam, "vlans", _doc([p]), ["containers", "edge-to-container"])
    # home
    p = Page("Home", seed=705, pretty=True)
    r = p.v("Router", 200, 40, 78, 53, ROUTER)
    for i, n in enumerate(["Laptop", "Phone", "TV", "Printer"]):
        h = p.v(n, 40 + i * 140, 220, 60, 60, PC)
        p.e(r, h, "wifi" if i % 2 else "", "endArrow=none;html=1;dashed=1;" if i % 2 else link)
    emit(fam, "home-network-pretty", _doc([p], nl=True, decl=True), ["pretty-printed", "xml-declaration"])


# --------------------------------------------------------------------------- cloud architecture

def cloud(emit):
    fam = "cloud"
    AWS = ("sketch=0;outlineConnect=0;fontColor=#232F3E;gradientColor=#F78E04;gradientDirection=north;fillColor=#D05C17;strokeColor=#ffffff;"
           "dashed=0;verticalLabelPosition=bottom;verticalAlign=top;align=center;html=1;fontSize=12;fontStyle=0;aspect=fixed;shape=mxgraph.aws4.resourceIcon;resIcon=mxgraph.aws4.{};")
    AWSG = "points=[[0,0],[0.25,0],[0.5,0],[0.75,0],[1,0],[1,0.25],[1,0.5],[1,0.75],[1,1],[0.75,1],[0.5,1],[0.25,1],[0,1],[0,0.75],[0,0.5],[0,0.25]];outlineConnect=0;gradientColor=none;html=1;whiteSpace=wrap;fontSize=12;fontStyle=0;container=1;pointerEvents=0;collapsible=0;recursiveResize=0;shape=mxgraph.aws4.group;grIcon=mxgraph.aws4.group_{};strokeColor=#248814;fillColor=none;verticalAlign=top;align=left;spacingLeft=30;fontColor=#AAB7B8;dashed=0;"
    AZ = "image;aspect=fixed;html=1;points=[];align=center;fontSize=12;image=img/lib/azure2/{}.svg;"
    GCP = "sketch=0;html=1;fillColor=#5184F3;strokeColor=none;verticalAlign=top;labelPosition=center;verticalLabelPosition=bottom;align=center;spacingTop=-6;fontSize=11;fontStyle=1;fontColor=#999999;shape=mxgraph.gcp2.{};"
    K8S = "sketch=0;html=1;dashed=0;whiteSpace=wrap;fillColor=#2875E2;strokeColor=#ffffff;points=[[0.005,0.63,0],[0.1,0.2,0],[0.9,0.2,0],[0.5,0,0],[0.995,0.63,0],[0.72,0.99,0],[0.5,1,0],[0.28,0.99,0]];verticalLabelPosition=bottom;align=center;verticalAlign=top;shape=mxgraph.kubernetes.icon2;prIcon={};"
    flow = "edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;endArrow=classic;"
    # aws two files
    for k, (n, comp) in enumerate([("three-tier-web", False), ("serverless-compressed", True)]):
        p = Page(n, seed=800 + k, enc="deflate" if comp else "plain", ids="rand" if comp else "seq")
        vpc = p.v("VPC", 20, 60, 640, 340, AWSG.format("vpc"))
        names = (["elastic_load_balancing", "ec2", "rds"] if not comp else ["api_gateway", "lambda", "dynamodb"])
        icons = [p.v(nm.replace("_", " ").title(), 60 + i * 200, 120, 78, 78, AWS.format(nm), parent=vpc) for i, nm in enumerate(names)]
        users = p.v("Users", -60, 140, 40, 40, "shape=actor;html=1;")
        p.e(users, icons[0], "", flow)
        for a, b in zip(icons, icons[1:]):
            p.e(a, b, "", flow)
        emit(fam, f"aws-{n}", _doc([p]), ["mxgraph.aws4", "container", "resIcon"])
    # azure
    for k, n in enumerate(["web-app", "data-platform"]):
        p = Page(n, seed=810 + k, enc="deflate" if k else "plain")
        res = ["compute/App_Services", "databases/SQL_Database", "storage/Storage_Accounts", "networking/Virtual_Networks"]
        ids = [p.v(r.split("/")[1].replace("_", " "), 60 + i * 170, 120, 68, 68, AZ.format(r)) for i, r in enumerate(res)]
        for a, b in zip(ids, ids[1:]):
            p.e(a, b, "", flow)
        emit(fam, f"azure-{n}", _doc([p]), ["image=img/lib", "svg-image"])
    # gcp
    for k, n in enumerate(["pipeline", "app-engine"]):
        p = Page(n, seed=820 + k)
        res = ["cloud_pubsub", "cloud_dataflow", "bigquery", "cloud_storage"]
        ids = [p.v(r, 60 + i * 170, 100, 50, 50, GCP.format(r)) for i, r in enumerate(res)]
        for a, b in zip(ids, ids[1:]):
            p.e(a, b, "", flow)
        emit(fam, f"gcp-{n}", _doc([p]), ["mxgraph.gcp2"])
    # k8s
    for k, n in enumerate(["cluster", "deployment-compressed"]):
        p = Page(n, seed=830 + k, enc="deflate" if k else "plain", ids="rand" if k else "seq")
        ns = p.v("namespace: prod", 20, 40, 560, 240, "rounded=1;whiteSpace=wrap;html=1;dashed=1;container=1;collapsible=0;verticalAlign=top;")
        svc = p.v("svc", 40, 100, 50, 48, K8S.format("svc"), parent=ns)
        for i in range(3):
            pod = p.v(f"pod-{i}", 200 + i * 110, 100, 50, 48, K8S.format("pod"), parent=ns)
            p.e(svc, pod, "", flow, parent=ns)
        emit(fam, f"k8s-{n}", _doc([p]), ["mxgraph.kubernetes", "container"])


# --------------------------------------------------------------------------- org chart

def orgchart(emit):
    fam = "orgchart"
    for k, (name, estyle, depth, branch, objects) in enumerate([
            ("small-tree", "edgeStyle=elbowEdgeStyle;elbow=vertical;rounded=0;html=1;endArrow=none;", 2, 3, False),
            ("deep-tree-compressed", ORTHO + "endArrow=none;", 4, 3, False),
            ("with-objects-placeholders", "edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;endArrow=none;", 3, 3, True),
            ("wide-tree", "endArrow=none;html=1;", 2, 8, False),
            ("curved-edges", "curved=1;endArrow=none;html=1;", 3, 2, False)]):
        rng = random.Random(900 + k)
        p = Page(name, seed=900 + k, enc="deflate" if "compressed" in name else "plain", ids="rand" if objects else "seq")
        counter = [0]
        leaf = [0]

        def build(level: int, parent_id: str | None) -> tuple[str, float]:
            i = counter[0]; counter[0] += 1
            person, title = PEOPLE[i % len(PEOPLE)], TITLES[min(level, len(TITLES) - 1)]
            kids = []
            if level < depth:
                for _ in range(branch if level else min(branch, 4)):
                    kids.append(build(level + 1, None))
            if kids:
                cx = sum(c[1] for c in kids) / len(kids)
            else:
                cx = leaf[0] * 150 + 75; leaf[0] += 1
            y = 30 + level * 120
            style = "rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;"
            if objects:
                nid = p.v(f"%name%<br><i>%title%</i>", cx - 65, y, 130, 60, style + "metaEdit=1;", obj=("object", {"placeholders": "1", "name": person, "title": title}))
            else:
                nid = p.v(f"{person}<br><i>{title}</i>", cx - 65, y, 130, 60, style)
            for cid, _ in kids:
                p.e(nid, cid, "", estyle)
            return nid, cx
        build(0, None)
        emit(fam, name, _doc([p]), ["unicode", "tree"] + (["object", "placeholders"] if objects else []))


# --------------------------------------------------------------------------- mockup / wireframe

def mockup(emit):
    fam = "mockup"
    BTN = "strokeWidth=1;shadow=0;dashed=0;align=center;html=1;shape=mxgraph.mockup.buttons.button;strokeColor=#666666;fontColor=#ffffff;mainText=;buttonStyle=round;fontSize=17;fontStyle=1;fillColor=#008cff;whiteSpace=wrap;"
    TXT = "strokeWidth=1;shadow=0;dashed=0;align=center;html=1;shape=mxgraph.mockup.text.textBox;fontColor=#666666;align=left;fontSize=17;spacingLeft=4;spacingTop=-3;whiteSpace=wrap;strokeColor=#666666;mainText=;"
    BROWSER = "strokeWidth=1;shadow=0;dashed=0;align=center;html=1;shape=mxgraph.mockup.containers.browserWindow;rSize=0;strokeColor=#666666;strokeColor2=#008cff;strokeColor3=#c4c4c4;mainText=,;recursiveResize=0;"
    CHECK = "strokeWidth=1;shadow=0;dashed=0;align=center;html=1;shape=mxgraph.mockup.forms.rrect;rSize=0;fillColor=#eeeeee;strokeColor=#999999;gradientColor=#cccccc;align=left;spacingLeft=4;fontSize=17;fontColor=#666666;labelPosition=right;"
    # login
    p = Page("Login", seed=1000)
    b = p.v("https://example.com/login", 20, 20, 400, 300, BROWSER)
    p.v("Username", 50, 100, 300, 36, TXT, parent=b); p.v("Password", 50, 150, 300, 36, TXT, parent=b)
    p.v("Sign in", 50, 210, 300, 40, BTN, parent=b)
    emit(fam, "login-page", _doc([p]), ["mxgraph.mockup", "nested-cells"])
    # sketchy
    p = Page("Sketch", seed=1001)
    s = "sketch=1;curveFitting=1;jiggle=2;fontFamily=Architects Daughter;fontSource=https%3A%2F%2Ffonts.googleapis.com%2Fcss%3Ffamily%3DArchitects%2BDaughter;"
    p.v("Header", 20, 20, 360, 50, "rounded=1;whiteSpace=wrap;html=1;" + s)
    p.v("Content", 20, 90, 360, 200, "rounded=0;whiteSpace=wrap;html=1;" + s)
    p.v("OK", 280, 310, 100, 40, "rounded=1;whiteSpace=wrap;html=1;fillColor=#d5e8d4;" + s)
    emit(fam, "sketch-style", _doc([p]), ["sketch=1", "fontSource-url"])
    # form with many rows
    p = Page("Form", seed=1002, enc="deflate")
    for i in range(10):
        p.v(f"Option {i}", 40, 30 + i * 36, 160, 26, CHECK)
    emit(fam, "form-checkboxes-compressed", _doc([p]), ["compressed", "mxgraph.mockup"])
    # dashboard
    p = Page("Dashboard", seed=1003)
    for i in range(6):
        p.v(f"<b>KPI {i}</b><br>{(i + 1) * 1234}", 30 + (i % 3) * 190, 30 + (i // 3) * 130, 170, 110, "rounded=1;whiteSpace=wrap;html=1;arcSize=6;fillColor=#f5f5f5;strokeColor=#666666;shadow=1;")
    emit(fam, "dashboard-cards", _doc([p]), ["html-label", "shadow"])
    # mobile
    p = Page("Mobile", seed=1004)
    ph = p.v("", 30, 20, 220, 420, "html=1;verticalLabelPosition=bottom;labelBackgroundColor=#ffffff;verticalAlign=top;shadow=0;dashed=0;strokeWidth=1;shape=mxgraph.android.phone2;strokeColor=#c0c0c0;")
    p.v("Home", 10, 40, 200, 40, BTN, parent=ph)
    emit(fam, "android-phone", _doc([p]), ["mxgraph.android"])
    # image placeholder + data uri
    from xmlkit import png_data_uri
    p = Page("Images", seed=1005)
    p.v("", 40, 40, 100, 100, "shape=image;html=1;verticalLabelPosition=bottom;verticalAlign=top;imageAspect=0;aspect=fixed;image=" + png_data_uri(4, 4, (20, 120, 220)) + ";")
    p.v("", 180, 40, 100, 100, "image;html=1;image=img/lib/mscae/Cloud.svg;")
    emit(fam, "images", _doc([p]), ["image=data:", "image=relative-path"])


# --------------------------------------------------------------------------- floorplan

def floorplan(emit):
    fam = "floorplan"
    WALL = "verticalLabelPosition=bottom;html=1;verticalAlign=top;align=center;shape=mxgraph.floorplan.wall;fillColor=#000000;strokeColor=none;"
    DOOR = "verticalLabelPosition=bottom;html=1;verticalAlign=top;align=center;shape=mxgraph.floorplan.doorRight;aspect=fixed;"
    WIN = "verticalLabelPosition=bottom;html=1;verticalAlign=top;align=center;shape=mxgraph.floorplan.window;"
    for k, (name, rooms) in enumerate([("apartment", 4), ("office-compressed", 8), ("rotated-furniture", 2), ("scaled-page", 3)]):
        extra = {}
        if name == "scaled-page":
            extra = dict(pageScale=0.5, pageWidth=1654, pageHeight=2339, gridSize=5, background="#f7f7f7")
        p = Page(name, seed=1100 + k, enc="deflate" if "compressed" in name else "plain",
                 model={**{"dx": 1000, "dy": 700, "grid": 1, "gridSize": 10, "guides": 1, "tooltips": 1, "connect": 1, "arrows": 1,
                           "fold": 1, "page": 1, "pageScale": 1, "pageWidth": 1169, "pageHeight": 827, "math": 0, "shadow": 0}, **extra})
        for r in range(rooms):
            ox, oy = 40 + (r % 4) * 220, 40 + (r // 4) * 200
            p.v("", ox, oy, 200, 10, WALL); p.v("", ox, oy + 190, 200, 10, WALL)
            p.v("", ox, oy, 10, 200, WALL); p.v("", ox + 190, oy, 10, 200, WALL)
            p.v("", ox + 60, oy - 3, 40, 16, DOOR); p.v("", ox + 130, oy + 187, 50, 16, WIN)
            p.v(f"Room {r + 1}", ox + 60, oy + 80, 80, 30, "text;html=1;align=center;verticalAlign=middle;")
        if name == "rotated-furniture":
            for i, a in enumerate([0, 30, 45, 90, 180, 270, -15]):
                p.v("", 60 + i * 90, 300, 60, 40, "verticalLabelPosition=bottom;html=1;verticalAlign=top;align=center;shape=mxgraph.floorplan.bed_double;" + f"rotation={a};")
        p.v("5.20 m", 40, 20, 200, 10, "shape=mxgraph.floorplan.dimension;html=1;", geo_attrs={})
        emit(fam, name, _doc([p]), ["mxgraph.floorplan", "rotation" if name == "rotated-furniture" else "walls"] + (["pageScale"] if extra else []))


# --------------------------------------------------------------------------- swimlanes

def swimlanes(emit):
    fam = "swimlanes"
    # horizontal pool + lanes
    p = Page("Cross-functional", seed=1200)
    pool = p.v("Process", 20, 20, 760, 380, "swimlane;html=1;childLayout=stackLayout;resizeParent=1;resizeParentMax=0;horizontal=0;startSize=20;horizontalStack=0;whiteSpace=wrap;")
    prev = None
    for i, lane in enumerate(["Customer", "Sales", "Warehouse"]):
        ln = p.v(lane, 20, i * 120 + 0, 740, 120, "swimlane;html=1;whiteSpace=wrap;startSize=20;horizontal=0;", parent=pool)
        t = p.v(f"Task {i}", 60 + i * 200, 30, 100, 50, ROUND, parent=ln)
        if prev:
            p.e(prev, t, "", ORTHO)
        prev = t
    emit(fam, "pool-horizontal-lanes", _doc([p]), ["swimlane", "stackLayout", "horizontal=0"])
    # vertical
    p = Page("Vertical", seed=1201, enc="deflate")
    for i, lane in enumerate(["Dev", "QA", "Ops"]):
        ln = p.v(lane, 20 + i * 220, 20, 200, 400, "swimlane;whiteSpace=wrap;html=1;startSize=26;")
        for j in range(3):
            p.v(f"{lane}-{j}", 40, 50 + j * 100, 120, 60, ROUND, parent=ln)
    emit(fam, "vertical-lanes-compressed", _doc([p]), ["swimlane", "compressed"])
    # kanban stack lists
    p = Page("Kanban", seed=1202)
    for i, col in enumerate(["Backlog", "Doing", "Done"]):
        c = p.v(col, 20 + i * 200, 20, 180, 30 + 50 * 4, "swimlane;fontStyle=1;childLayout=stackLayout;horizontal=1;startSize=30;horizontalStack=0;resizeParent=1;resizeParentMax=0;resizeLast=0;collapsible=0;marginBottom=0;whiteSpace=wrap;html=1;")
        for j in range(3 - i + 1):
            p.v(f"Card {i}.{j}", 0, 30 + j * 50, 180, 50, "text;strokeColor=none;fillColor=none;align=left;verticalAlign=middle;spacingLeft=4;spacingRight=4;overflow=hidden;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;rotatable=0;whiteSpace=wrap;html=1;", parent=c)
    emit(fam, "kanban-stack-layout", _doc([p]), ["stackLayout", "swimlane"])
    # nested pool > lane > container
    p = Page("Nested", seed=1203)
    pool = p.v("Pool", 20, 20, 600, 360, "swimlane;horizontal=0;startSize=20;html=1;whiteSpace=wrap;")
    lane = p.v("Lane", 20, 0, 580, 360, "swimlane;horizontal=0;startSize=20;html=1;whiteSpace=wrap;", parent=pool)
    grp = p.v("Group", 40, 40, 500, 280, "swimlane;html=1;whiteSpace=wrap;startSize=26;swimlaneLine=0;container=1;", parent=lane)
    p.v("Leaf", 40, 60, 100, 50, ROUND, parent=grp)
    emit(fam, "nested-pool-lane-group", _doc([p]), ["nested-containers", "swimlaneLine"])
    # collapsed swimlane with alternateBounds
    p = Page("Collapsed", seed=1204)
    c = p.v("Collapsed lane", 40, 40, 200, 26, "swimlane;whiteSpace=wrap;html=1;collapsible=1;startSize=26;",
            cell_attrs={"collapsed": "1"}, geo_children=[el("mxRectangle", {"x": 40, "y": 40, "width": 200, "height": 160, "as": "alternateBounds"})])
    p.v("Hidden child", 20, 50, 100, 40, ROUND, parent=c)
    emit(fam, "collapsed-with-alternate-bounds", _doc([p]), ["collapsed=1", "alternateBounds"])
    # swimlane with rotated header, fill variants, many lanes
    p = Page("Many", seed=1205, ids="rand")
    for i in range(10):
        p.v(f"Lane {i}", 20, 20 + i * 70, 800, 70, f"swimlane;horizontal=0;startSize=30;html=1;whiteSpace=wrap;swimlaneFillColor={'#f5f5f5' if i % 2 else 'none'};fillColor=#{(i * 40) % 255:02x}{(i * 70) % 255:02x}{(i * 90) % 255:02x};")
    emit(fam, "ten-lanes", _doc([p]), ["swimlane", "rand-ids"])


# --------------------------------------------------------------------------- whiteboard

def whiteboard(emit):
    fam = "whiteboard"
    SK = "sketch=1;curveFitting=1;jiggle=2;"
    STICKY = "shape=note;whiteSpace=wrap;html=1;backgroundOutline=1;darkOpacity=0.05;fillColor={};strokeColor=none;" + SK
    # sticky notes
    p = Page("Stickies", seed=1300)
    cols = ["#fff2cc", "#dae8fc", "#d5e8d4", "#f8cecc", "#e1d5e7"]
    texts = ["Idea \U0001F4A1", "Ship it \U0001F680", "Bug \U0001F41B", "Спросить команду", "質問がある", "Why?", "TODO ✔", "משימה"]
    for i, t in enumerate(texts):
        p.v(t, 30 + (i % 4) * 150, 30 + (i // 4) * 150, 130, 130, STICKY.format(cols[i % len(cols)]), geo_attrs={})
    emit(fam, "sticky-notes", _doc([p]), ["emoji", "unicode", "sketch=1"])
    # mind map
    p = Page("Mind map", seed=1301, enc="deflate", ids="rand")
    c = p.v("Central topic", 280, 200, 140, 70, "ellipse;whiteSpace=wrap;html=1;fillColor=#fff2cc;" + SK)
    import math
    for i in range(7):
        a = i * 2 * math.pi / 7
        n = p.v(f"Branch {i + 1}", 280 + 260 * math.cos(a), 200 + 200 * math.sin(a), 100, 40, "rounded=1;whiteSpace=wrap;html=1;arcSize=50;" + SK)
        p.e(c, n, "", "curved=1;endArrow=none;html=1;" + SK)
    emit(fam, "mind-map-curved", _doc([p]), ["compressed", "curved-edges", "sketch=1"])
    # freeform text only
    p = Page("Text", seed=1302)
    p.v("Retro \U0001F389", 40, 30, 300, 50, "text;html=1;strokeColor=none;fillColor=none;align=left;verticalAlign=middle;whiteSpace=wrap;rounded=0;fontSize=28;fontStyle=1;fontFamily=Comic Sans MS;")
    p.v("went well<br>• tests<br>• docs", 40, 100, 200, 100, "text;html=1;align=left;verticalAlign=top;whiteSpace=wrap;")
    p.v("to improve", 280, 100, 200, 100, "text;html=1;align=left;verticalAlign=top;whiteSpace=wrap;fontColor=#CC0000;")
    emit(fam, "text-only", _doc([p]), ["text-style", "html-label"])
    # shapes with callouts and arrows
    p = Page("Callouts", seed=1303)
    a = p.v("Look here!", 40, 40, 140, 80, "shape=callout;whiteSpace=wrap;html=1;perimeter=calloutPerimeter;position2=0.3;" + SK)
    b = p.v("", 300, 40, 100, 100, "shape=mxgraph.basic.star;whiteSpace=wrap;html=1;" + SK)
    p.e(a, b, "", "endArrow=classic;html=1;strokeWidth=3;strokeColor=#FF0000;" + SK, points=[(240, 160)])
    p.v("", 40, 200, 160, 80, "shape=cloud;whiteSpace=wrap;html=1;" + SK)
    emit(fam, "callouts-and-basics", _doc([p]), ["mxgraph.basic", "sketch=1"])
    # lots of loose shapes, desktop attrs
    p = Page("Scatter", seed=1304)
    rng = random.Random(1304)
    for i in range(60):
        st = rng.choice(["ellipse", "rounded=1", "shape=triangle", "rhombus", "shape=hexagon", "shape=cylinder3", "shape=note"])
        p.v(rng.choice(["", "x", "★", "α", "42"]), rng.randrange(0, 900), rng.randrange(0, 600), rng.randrange(20, 90), rng.randrange(20, 90),
            f"{st};whiteSpace=wrap;html=1;fillColor=#{rng.randrange(0x1000000):06x};rotation={rng.choice([0, 0, 15, 45, 90])};" + SK)
    emit(fam, "scatter-60", _doc([p], MXFILE_DESKTOP), ["rotation", "sketch=1", "desktop-attrs"])


def generate(emit):
    for fn in (flowcharts, uml_class, uml_sequence, uml_activity, uml_state, bpmn, er_tables, network, cloud,
               orgchart, mockup, floorplan, swimlanes, whiteboard):
        fn(emit)
