#!/usr/bin/env python3
"""Generate city-themed Shunt maps.

Layout philosophy (matches New York / the current game direction):
  * No dead ends. Every node has degree >= 2. All track lives on closed loops.
  * Switches are degree-3 nodes. Crossovers are modelled as S-curves that leave
    each rail *horizontally*, so the loader's turnout detection unambiguously
    groups the diverging track with the continuing rail (stem = the approach).
  * The loader builds geometry from the `polyline`, so we sample cubic beziers
    straight into it. `control_x/control_y` are only editor hints.

Coordinates are in map units (~0..1300 x, 0..700 y); the game scales by 0.04.
"""

import json, math, os

SCALE = 0.04  # game-side, only used here to sanity-check world sizes


def cubic(p0, c1, c2, p3, n=14):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        x = u*u*u*p0[0] + 3*u*u*t*c1[0] + 3*u*t*t*c2[0] + t*t*t*p3[0]
        y = u*u*u*p0[1] + 3*u*u*t*c1[1] + 3*u*t*t*c2[1] + t*t*t*p3[1]
        pts.append([round(x, 2), round(y, 2)])
    return pts


def polylen(poly):
    L = 0.0
    for a, b in zip(poly, poly[1:]):
        L += math.hypot(b[0]-a[0], b[1]-a[1])
    return round(L, 2)


class Builder:
    def __init__(self):
        self.nodes = []          # {id,x,y}
        self.edges = []          # raw edge dicts
        self._by_xy = {}
        self.drops = []
        self.spawns = []

    def node(self, x, y):
        key = (round(x, 1), round(y, 1))
        if key in self._by_xy:
            return self._by_xy[key]
        nid = len(self.nodes)
        self.nodes.append({"id": nid, "x": round(x, 2), "y": round(y, 2)})
        self._by_xy[key] = nid
        return nid

    def _xy(self, nid):
        return (self.nodes[nid]["x"], self.nodes[nid]["y"])

    def line(self, a, b):
        pa, pb = self._xy(a), self._xy(b)
        poly = [[pa[0], pa[1]], [pb[0], pb[1]]]
        self.edges.append({"from": a, "to": b, "polyline": poly,
                           "length": polylen(poly), "curved": False,
                           "control_x": (pa[0]+pb[0])/2, "control_y": (pa[1]+pb[1])/2})

    def arc(self, a, b, c1, c2):
        pa, pb = self._xy(a), self._xy(b)
        poly = cubic(pa, c1, c2, pb)
        self.edges.append({"from": a, "to": b, "polyline": poly,
                           "length": polylen(poly), "curved": True,
                           "control_x": round(c1[0], 2), "control_y": round(c1[1], 2)})

    def rail(self, y, xs):
        """Straight horizontal rail through sorted x positions -> {x: nodeid}."""
        xs = sorted(set(xs))
        m = {x: self.node(x, y) for x in xs}
        for x0, x1 in zip(xs, xs[1:]):
            self.line(m[x0], m[x1])
        return m

    def crossover(self, top, bot, bulge=0.55):
        """S-curve between a top-rail node and a bottom-rail node, leaving each
        rail horizontally toward the other's x (a real-looking turnout pair)."""
        pt, pb = self._xy(top), self._xy(bot)
        dx = pb[0] - pt[0]
        c1 = (pt[0] + dx*bulge, pt[1])
        c2 = (pb[0] - dx*bulge, pb[1])
        poly = cubic(pt, c1, c2, pb)
        self.edges.append({"from": top, "to": bot, "polyline": poly,
                           "length": polylen(poly), "curved": True,
                           "control_x": round(c1[0], 2), "control_y": round(c1[1], 2)})

    def dropoff(self, colour, nid):
        self.drops.append({"colour": colour, "node": nid})

    def spawn(self, x, y):
        self.spawns.append({"x": round(x, 2), "y": round(y, 2), "colour": -1})

    # -- clearance-aware spawning -------------------------------------------
    def _switch_ids(self):
        deg = {n["id"]: 0 for n in self.nodes}
        for e in self.edges:
            deg[e["from"]] += 1
            deg[e["to"]] += 1
        return {i for i, d in deg.items() if d >= 3}, deg

    def _midpoint_clearance(self, edge):
        """Shortest track distance from an edge's midpoint to the nearest switch
        node — the same quantity the game's placeCarsFromSpawns() guards on."""
        import heapq
        switches, _ = self._switch_ids()
        adj = {n["id"]: [] for n in self.nodes}
        for e in self.edges:
            adj[e["from"]].append((e["to"], e["length"]))
            adj[e["to"]].append((e["from"], e["length"]))
        a, b, L = edge["from"], edge["to"], edge["length"]
        # temp midpoint node M -> a,b each L/2
        best = {a: L/2, b: L/2}
        pq = [(L/2, a), (L/2, b)]
        found = float("inf")
        while pq:
            d, u = heapq.heappop(pq)
            if d > best.get(u, float("inf")):
                continue
            if u in switches:
                found = min(found, d)
                continue  # switches don't conduct further for this purpose
            for v, w in adj[u]:
                nd = d + w
                if nd < best.get(v, float("inf")):
                    best[v] = nd
                    heapq.heappush(pq, (nd, v))
        return found

    def auto_spawns(self, count, min_clear=90):
        """Place `count` spawns at midpoints of the straightest, best-cleared
        rail segments, guaranteeing the game will accept them."""
        cands = []
        for e in self.edges:
            if e["curved"]:
                continue
            c = self._midpoint_clearance(e)
            if c > min_clear:
                mx = (self._xy(e["from"])[0] + self._xy(e["to"])[0]) / 2
                my = (self._xy(e["from"])[1] + self._xy(e["to"])[1]) / 2
                cands.append((c, mx, my))
        cands.sort(reverse=True)
        for _, mx, my in cands[:count]:
            self.spawn(mx, my)

    def build(self, car_count):
        deg = {n["id"]: 0 for n in self.nodes}
        for e in self.edges:
            deg[e["from"]] += 1
            deg[e["to"]] += 1
        nodes = []
        for n in self.nodes:
            d = deg[n["id"]]
            nodes.append({**n, "kind": "switch" if d >= 3 else "junction", "degree": d})
        edges = []
        for i, e in enumerate(self.edges):
            edges.append({"id": i, "from": e["from"], "to": e["to"],
                          "polyline": e["polyline"], "length": e["length"],
                          "has_signal": False, "buffer_end": False,
                          "curved": e["curved"],
                          "control_x": e["control_x"], "control_y": e["control_y"]})
        return {"nodes": nodes, "edges": edges, "drop_offs": self.drops,
                "spawns": self.spawns, "car_count": car_count}


# ---- validation -------------------------------------------------------------

def validate(name, m):
    nodes = {n["id"]: n for n in m["nodes"]}
    adj = {i: [] for i in nodes}
    for e in m["edges"]:
        adj[e["from"]].append(e["to"])
        adj[e["to"]].append(e["from"])
    errs = []
    # degree recorded matches incidence
    for nid, n in nodes.items():
        if n["degree"] != len(adj[nid]):
            errs.append(f"node {nid} degree {n['degree']} != incidence {len(adj[nid])}")
        if len(adj[nid]) < 2:
            errs.append(f"node {nid} is a dead end (degree {len(adj[nid])})")
    # connectivity
    seen, stack = set(), [next(iter(nodes))]
    while stack:
        c = stack.pop()
        if c in seen:
            continue
        seen.add(c)
        stack.extend(adj[c])
    if len(seen) != len(nodes):
        errs.append(f"graph not connected: {len(seen)}/{len(nodes)} reachable")
    # drop-offs: 4 distinct colours on valid nodes
    cols = sorted(d["colour"] for d in m["drop_offs"])
    if cols != [0, 1, 2, 3]:
        errs.append(f"drop-off colours {cols} != [0,1,2,3]")
    for d in m["drop_offs"]:
        if d["node"] not in nodes:
            errs.append(f"drop-off node {d['node']} missing")
    # switch nodes must be degree 3 (avoid degree-4 crossings)
    for n in m["nodes"]:
        if n["kind"] == "switch" and n["degree"] != 3:
            errs.append(f"switch node {n['id']} has degree {n['degree']} (want 3)")
    if len(m["spawns"]) < 4:
        errs.append(f"only {len(m['spawns'])} spawns")
    # spawns must sit on track AND clear switches by > 80 map units (2*carLen),
    # or the game's placeCarsFromSpawns() drops them and no cars appear.
    import heapq
    switches = {n["id"] for n in m["nodes"] if n["degree"] >= 3}
    adjw = {n["id"]: [] for n in m["nodes"]}
    for e in m["edges"]:
        adjw[e["from"]].append((e["to"], e["length"]))
        adjw[e["to"]].append((e["from"], e["length"]))

    def seg_dist(px, py, a, b):
        ax, ay = a; bx, by = b
        dx, dy = bx-ax, by-ay
        L2 = dx*dx + dy*dy
        t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px-ax)*dx + (py-ay)*dy)/L2))
        return math.hypot(px-(ax+t*dx), py-(ay+t*dy)), t

    def clearance(px, py):
        best_e, best_d, best_t = None, 1e9, 0
        for e in m["edges"]:
            for a, b in zip(e["polyline"], e["polyline"][1:]):
                d, t = seg_dist(px, py, a, b)
                if d < best_d:
                    best_d, best_e, best_t = d, e, None
        # distance along the found edge from the projected point to each endpoint
        e = best_e
        L = e["length"]
        # approximate split by nearest endpoint fractions using straight edge
        dA = math.hypot(px-m_nodes[e["from"]]["x"], py-m_nodes[e["from"]]["y"])
        dB = math.hypot(px-m_nodes[e["to"]]["x"], py-m_nodes[e["to"]]["y"])
        dist = {e["from"]: min(dA, L), e["to"]: min(dB, L)}
        pq = [(dist[e["from"]], e["from"]), (dist[e["to"]], e["to"])]
        found = 1e9
        seen = {}
        while pq:
            d, u = heapq.heappop(pq)
            if d > seen.get(u, 1e9):
                continue
            seen[u] = d
            if u in switches:
                found = min(found, d); continue
            for v, w in adjw[u]:
                nd = d + w
                if nd < seen.get(v, 1e9):
                    seen[v] = nd; heapq.heappush(pq, (nd, v))
        return best_d, found

    m_nodes = nodes
    for s in m["spawns"]:
        onTrack, clr = clearance(s["x"], s["y"])
        if onTrack > 20:
            errs.append(f"spawn ({s['x']},{s['y']}) is {onTrack:.0f} off track")
        elif clr < 82:
            errs.append(f"spawn ({s['x']},{s['y']}) clears switch by only {clr:.0f} (<82)")
    return errs


# ---- city layouts -----------------------------------------------------------

def oval(b, xs_top, xs_bot, top, bot, r=140):
    """Close two rails into a loop with U-turn end curves. Returns (tmap,bmap)."""
    tmap = b.rail(top, xs_top)
    bmap = b.rail(bot, xs_bot)
    xl_t, xr_t = min(xs_top), max(xs_top)
    xl_b, xr_b = min(xs_bot), max(xs_bot)
    # left U-turn: top-left down to bottom-left, bulging further left
    b.arc(tmap[xl_t], bmap[xl_b], (xl_t - r, top), (xl_b - r, bot))
    # right U-turn: bottom-right up to top-right, bulging further right
    b.arc(bmap[xr_b], tmap[xr_t], (xr_b + r, bot), (xr_t + r, top))
    return tmap, bmap


# Crossovers put a switch on each rail; keep those switches >=200 map units apart
# so every rail has long clear straights for cars to stand on.

def london():
    """Broad two-track loop, four well-spaced crossovers — a classic through yard."""
    b = Builder()
    top, bot = 150, 470
    cx = [300, 560, 820, 1080]                      # crossover x anchors (~260 apart)
    off = 120
    drops_t = [430, 970]
    drops_b = [690, 950]
    xt = [120] + cx + drops_t + [1300]
    xb = [120] + [x+off for x in cx] + drops_b + [1300]
    tmap, bmap = oval(b, xt, xb, top, bot, r=170)
    for x in cx:
        b.crossover(tmap[x], bmap[x+off])           # fan down-right
    b.dropoff(0, tmap[430]); b.dropoff(1, tmap[970])
    b.dropoff(2, bmap[690]); b.dropoff(3, bmap[950])
    b.auto_spawns(8)
    return "London", b.build(20)


def berlin():
    """Tall, narrow loop with four crossovers — a deep inner-city depot."""
    b = Builder()
    top, bot = 110, 590
    cx = [280, 520, 760, 1000]
    off = 110
    drops_t = [400, 880]
    drops_b = [260, 640]
    xt = [140] + cx + drops_t + [1180]
    xb = [140] + [x+off for x in cx] + drops_b + [1180]
    tmap, bmap = oval(b, xt, xb, top, bot, r=190)
    for x in cx:
        b.crossover(tmap[x], bmap[x+off])
    b.dropoff(0, tmap[400]); b.dropoff(1, tmap[880])
    b.dropoff(2, bmap[260]); b.dropoff(3, bmap[640])
    b.auto_spawns(8)
    return "Berlin", b.build(20)


def tokyo():
    """Outer loop wrapping a concentric inner loop, linked by crossovers — a ring yard."""
    b = Builder()
    ot, ob = 110, 630          # outer top/bottom
    it, ib = 270, 470          # inner top/bottom
    # outer rails: switches where crossovers meet them
    txo = [360, 1060]          # outer-top crossover anchors
    bxo = [360, 1060]          # outer-bottom crossover anchors
    xt = [140, 700, 1320] + txo
    xb = [140, 700, 1320] + bxo
    tmap, bmap = oval(b, xt, xb, ot, ob, r=210)
    # inner loop (its own closed oval)
    ixt = [420, 640, 860, 1000]
    ixb = [420, 640, 860, 1000]
    itmap, ibmap = oval(b, ixt, ixb, it, ib, r=110)
    # link outer<->inner with crossovers (S-curves span the rail gap)
    b.crossover(tmap[txo[0]], itmap[420])
    b.crossover(itmap[1000], tmap[txo[1]])
    b.crossover(ibmap[640], bmap[bxo[0]])
    b.crossover(bmap[bxo[1]], ibmap[860])
    b.dropoff(0, tmap[700]); b.dropoff(1, bmap[700])
    b.dropoff(2, itmap[640]); b.dropoff(3, ibmap[420])
    b.auto_spawns(8)
    return "Tokyo", b.build(20)


def sydney():
    """Long ladder yard: one loop, five same-hand crossovers — a big sorting bowl."""
    b = Builder()
    top, bot = 160, 460
    cx = [280, 500, 720, 940, 1160]
    off = 100
    drops_t = [400, 1060]
    drops_b = [620, 840]
    xt = [120] + cx + drops_t + [1360]
    xb = [120] + [x+off for x in cx] + drops_b + [1360]
    tmap, bmap = oval(b, xt, xb, top, bot, r=170)
    for x in cx:
        b.crossover(tmap[x], bmap[x+off])
    b.dropoff(0, tmap[400]); b.dropoff(1, tmap[1060])
    b.dropoff(2, bmap[620]); b.dropoff(3, bmap[840])
    b.auto_spawns(9)
    return "Sydney", b.build(22)


def denver():
    """Loop with two scissors (X) crossovers set far apart — a junction with reach."""
    b = Builder()
    top, bot = 150, 510
    # scissors bundles: an X of two opposing crossovers between the rails,
    # spaced well apart so long clear straights remain between them.
    sc = [(360, 500), (1020, 1160)]   # (left, right) span of each scissors
    drops_t = [720, 1300]
    drops_b = [780, 260]
    sc_top = [x for pair in sc for x in pair]
    xt = [140] + sc_top + drops_t + [1440]
    xb = [140] + sc_top + drops_b + [1440]
    tmap, bmap = oval(b, xt, xb, top, bot, r=180)
    for lft, rgt in sc:
        b.crossover(tmap[lft], bmap[rgt])   # down-right
        b.crossover(bmap[lft], tmap[rgt])   # up-right (crosses the first -> X)
    b.dropoff(0, tmap[720]); b.dropoff(1, bmap[780])
    b.dropoff(2, bmap[260]); b.dropoff(3, tmap[1300])
    b.auto_spawns(8)
    return "Denver", b.build(20)


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "Maps")
    out_dir = os.path.abspath(out_dir)
    ok = True
    for fn in (london, berlin, tokyo, sydney, denver):
        name, m = fn()
        errs = validate(name, m)
        status = "OK " if not errs else "BAD"
        print(f"[{status}] {name}: nodes={len(m['nodes'])} edges={len(m['edges'])} "
              f"switches={sum(1 for n in m['nodes'] if n['kind']=='switch')} "
              f"drops={sorted(d['colour'] for d in m['drop_offs'])} "
              f"spawns={len(m['spawns'])} cars={m['car_count']}")
        for e in errs:
            ok = False
            print("      !", e)
        if not errs:
            with open(os.path.join(out_dir, f"{name}.json"), "w") as f:
                json.dump(m, f, indent=2)
    print("ALL VALID" if ok else "VALIDATION FAILED")


if __name__ == "__main__":
    main()
