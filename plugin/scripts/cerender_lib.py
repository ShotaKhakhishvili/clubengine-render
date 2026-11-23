"""cerender_lib: tools for writing clubengine-render scenes as Python scripts.

A scene script builds a Scene and calls write(), which prints the scene JSON. cerender.py runs the
script and renders what it prints (python cerender.py render scene.py --preview):

    from cerender_lib import Scene, scatter

    s = Scene(duration=6, width=1080, height=1350)
    s.environment(sky=["#8fc8ff", "#fff1e3", "#ffe0c7"], ground="#ffe0c7")
    for i, p in enumerate(scatter(40, {"center": [0, 0, 0], "radius": 8}, min_gap=1.2, seed=3)):
        s.add(f"tree{i}", mesh="cone", color="#5fa86a", position=p, pivot="bottom", scale=[0.6, 0.6, 1.4])
    s.light("sun", position=[5, -6, 7], look_at=[0, 0, 0])
    s.camera("cam", position=[6, -6, 4], look_at=[0, 0, 0.5])
    s.write()

Tools, not things: everything maps 1:1 onto the scene format in reference.md. Keyword arguments
use the format's key names; snake_case is accepted too (look_at -> lookAt, min_gap -> minGap).
Prefer engine features (emitters, repeat, wave, path, walk) to baked keys: bake() is the last resort.
Standard library only.
"""

from __future__ import annotations

import json
import math
import random
import sys

__all__ = ["Scene", "rng", "scatter", "circle", "smooth", "path", "look_along", "bake"]


def _camel(key: str) -> str:
    head, *rest = key.split("_")
    return head + "".join(part[:1].upper() + part[1:] for part in rest)


def _keys(d: dict) -> dict:
    """snake_case keys to the format's camelCase, recursively (values are left alone)."""
    return {_camel(k): (_keys(v) if isinstance(v, dict) else v) for k, v in d.items() if v is not None}


def _round(value, digits: int):
    if isinstance(value, float):
        r = round(value, digits)
        return int(r) if r == int(r) and abs(r) < 1e15 else r
    if isinstance(value, (list, tuple)):
        return [_round(v, digits) for v in value]
    if isinstance(value, dict):
        return {k: _round(v, digits) for k, v in value.items()}
    return value


class Scene:
    """The whole scene. Builders append to it; write() prints it as compact JSON."""

    def __init__(self, duration: float = 0, fps: int = 30, width: int = 1080, height: int = 1080):
        self.output = {"width": width, "height": height, "fps": fps, "duration": duration}
        self.env: dict = {}
        self.materials: dict = {}
        self.objects: list = []
        self.animations: list = []

    def environment(self, **keys) -> None:
        """sky, ground, ambient, fog, exposure (merged into what's there)."""
        self.env.update(_keys(keys))

    def material(self, name: str, **keys) -> str:
        """A named material (color, roughness, metallic, emissive, unlit). Returns the name."""
        self.materials[name] = _keys(keys)
        return name

    def add(self, id: str, **keys) -> str:
        """Any object, keys exactly as in the format (mesh, color, position, parent, emitter, ...). Returns the id."""
        self.objects.append({"id": id, **_keys(keys)})
        return id

    def group(self, id: str, **keys) -> str:
        """An invisible pivot to parent other objects to."""
        return self.add(id, **keys)

    def light(self, id: str, type: str = "directional", position=(5, -6, 7), look_at=(0, 0, 0), **keys) -> str:
        """Light keys (color, intensity, radius, inner_angle, outer_angle) go into "light", the rest onto the object."""
        light = {k: keys.pop(k) for k in list(keys) if _camel(k) in ("color", "intensity", "radius", "innerAngle", "outerAngle")}
        return self.add(id, light={"type": type, **_keys(light)}, position=list(position), look_at=look_at, **keys)

    def camera(self, id: str = "cam", position=(4, -4, 2.5), look_at=(0, 0, 0.5), **keys) -> str:
        return self.add(id, camera={}, position=list(position), look_at=look_at, **keys)

    def emitter(self, id: str, mesh: str, count: int, area: dict, size=None, scale=None, rotation=None,
                motion=None, loop=None, min_gap=None, avoid=None, seed=None, **keys) -> str:
        """An emitter: these arguments go into its "emitter" block (size, scale and rotation are per instance),
        the rest (color or a list of colors, material, shape options, pivot, shadow, position, parent) onto the object."""
        block = _keys({"count": count, "area": area, "size": size, "scale": scale, "rotation": rotation,
                       "motion": motion, "loop": loop, "min_gap": min_gap, "avoid": avoid, "seed": seed})
        return self.add(id, mesh=mesh, emitter=block, **keys)

    def animate(self, target, **track) -> None:
        """animate("ball.position.z", keys=[[0, 0.5], [1, 2]]) adds one track, keys as in the format.
        animate(track) or animate([tracks]) adds tracks made by path(), look_along() or bake()."""
        if isinstance(target, str):
            self.animations.append({"target": target, **_keys(track)})
        else:
            self.animations.extend([target] if isinstance(target, dict) else target)

    def to_dict(self, digits: int = 4) -> dict:
        scene = {"version": 1, "output": self.output}
        if self.env:
            scene["environment"] = self.env
        if self.materials:
            scene["materials"] = self.materials
        scene["objects"] = self.objects
        if self.animations:
            scene["animations"] = self.animations
        return _round(scene, digits)

    def write(self, digits: int = 4) -> None:
        """Prints the scene as compact JSON (numbers rounded to `digits` decimals) for cerender.py."""
        sys.stdout.write(json.dumps(self.to_dict(digits), separators=(",", ":")))
        sys.stdout.write("\n")


def rng(seed: int = 1) -> random.Random:
    """A seeded random generator, so the scene comes out the same every run."""
    return random.Random(seed)


def _in_area(area: dict, r: random.Random) -> list:
    if "min" in area:
        lo, hi = area["min"], area["max"]
        return [lo[i] + (hi[i] - lo[i]) * r.random() for i in range(3)]
    cx, cy, cz = area.get("center", (0, 0, 0))
    inner, radius = area.get("inner", 0), area["radius"]
    d = math.sqrt(inner * inner + r.random() * (radius * radius - inner * inner))
    a = 2 * math.pi * r.random()
    return [cx + d * math.cos(a), cy + d * math.sin(a), cz + area.get("height", 0) * r.random()]


def _avoided(p, avoid) -> bool:
    for a in avoid:
        if "radius" in a:
            if (p[0] - a["center"][0]) ** 2 + (p[1] - a["center"][1]) ** 2 < a["radius"] ** 2:
                return True
        elif a["min"][0] <= p[0] <= a["max"][0] and a["min"][1] <= p[1] <= a["max"][1]:
            return True
    return False


def scatter(count: int, area: dict, min_gap: float = 0, avoid=(), seed: int = 1, attempts: int = 30) -> list:
    """Up to `count` points [x, y, z] in an area, at least `min_gap` apart horizontally and outside `avoid`.
    Areas and avoid regions are written as in emitters: {"min", "max"} boxes, {"center", "radius", "inner", "height"} discs,
    avoid [{"center": [x, y], "radius": r} or {"min": [x, y], "max": [x, y]}]. For plain identical copies, an emitter is
    cheaper; scatter() is for placing different things (houses, trees built from parts) or reusing the spots."""
    r = rng(seed)
    cell = min_gap if min_gap > 0 else 1.0
    grid: dict = {}
    points = []
    for _ in range(count):
        for _ in range(attempts):
            p = _in_area(area, r)
            if _avoided(p, avoid):
                continue
            cx, cy = int(math.floor(p[0] / cell)), int(math.floor(p[1] / cell))
            if min_gap > 0 and any((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 < min_gap * min_gap
                                   for dx in (-1, 0, 1) for dy in (-1, 0, 1) for q in grid.get((cx + dx, cy + dy), ())):
                continue
            grid.setdefault((cx, cy), []).append(p)
            points.append(p)
            break
    if len(points) < count:
        print(f"cerender_lib: scatter placed {len(points)} of {count} points (min_gap/avoid leave no room)", file=sys.stderr)
    return points


def circle(center=(0, 0, 0), radius: float = 1, count: int = 8, start: float = 0) -> list:
    """`count` points evenly around a horizontal circle, from `start` degrees (0 = +X side)."""
    a0 = math.radians(start)
    return [[center[0] + radius * math.cos(a0 + 2 * math.pi * i / count),
             center[1] + radius * math.sin(a0 + 2 * math.pi * i / count), center[2]] for i in range(count)]


def smooth(points, samples: int = 8, loop: bool = False) -> list:
    """A smooth curve through the points (the same Catmull-Rom curve "path" uses), `samples` points per segment.
    For placing things along a curve; to move something along one, use path()."""
    pts = [list(p) for p in points]
    n = len(pts)
    if n < 2:
        return pts
    at = (lambda k: pts[k % n]) if loop else (lambda k: pts[min(max(k, 0), n - 1)])
    out = []
    for s in range(n if loop else n - 1):
        p0, p1, p2, p3 = at(s - 1), at(s), at(s + 1), at(s + 2)
        for k in range(0 if s == 0 else 1, samples + 1):
            if loop and s == n - 1 and k == samples:
                break
            t = k / samples
            t2, t3 = t * t, t * t * t
            out.append([0.5 * (2 * p1[i] + (p2[i] - p0[i]) * t + (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * t2
                               + (3 * p1[i] - p0[i] - 3 * p2[i] + p3[i]) * t3) for i in range(3)])
    return out


def path(target: str, points, duration: float = None, start: float = 0, loop: bool = False, face: bool = False,
         curve: str = "smooth", ease: str = None) -> dict:
    """A "path" track: `target` (an id, or "id.lookAt") travels through the points at an even speed."""
    if "." not in target:
        target += ".position"
    spec = {"points": [list(p) for p in points], "curve": curve if curve != "smooth" else None, "start": start or None,
            "duration": duration, "loop": loop or None, "face": face or None, "ease": ease}
    return {"target": target, "path": _keys(spec)}


def _length(points, loop: bool) -> float:
    pts = smooth(points, 16, loop)
    if loop:
        pts.append(pts[0])
    return sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))


def look_along(camera: str, points, duration: float, start: float = 0, ahead: float = 0.6, loop: bool = False) -> list:
    """A camera flying along the points in `duration` seconds while looking where it's going: two path tracks,
    the lookAt one `ahead` seconds in front (on an open path it runs on a bit past the end). Pass them to Scene.animate."""
    points = [list(p) for p in points]
    eye = path(camera + ".position", points, duration, start, loop)
    if loop:
        return [eye, path(camera + ".lookAt", points, duration, start - ahead, loop)]

    # Same speed as the eye, continuing straight on past the last point
    last, before = points[-1], points[-2]
    gaze_points = points + [[2 * last[i] - before[i] for i in range(3)]]
    gaze_duration = duration * _length(gaze_points, False) / _length(points, False)
    return [eye, path(camera + ".lookAt", gaze_points, gaze_duration, start - ahead)]


def bake(target: str, fn, start: float, end: float, fps: int = 30, tolerance: float = 1e-3) -> dict:
    """A keys track sampling fn(t) (a number, or [x, y, z]) from start to end at fps, in the compact key form.
    Keys that a straight line between their neighbours already gives (within tolerance) are dropped.
    Only for motion no engine feature covers."""
    count = max(1, int(round((end - start) * fps)))
    samples = []
    for i in range(count + 1):
        t = start + (end - start) * i / count
        v = fn(t)
        samples.append([t, *(v if isinstance(v, (list, tuple)) else [v])])

    # Keep a sample when the line from the last kept key to the next sample misses one in between
    keys, last = [samples[0]], 0
    for i in range(1, len(samples) - 1):
        a, c = samples[last], samples[i + 1]
        if i - last >= 60 or any(abs(a[j] + (c[j] - a[j]) * (b[0] - a[0]) / (c[0] - a[0]) - b[j]) > tolerance
                                 for b in samples[last + 1:i + 1] for j in range(1, len(b))):
            keys.append(samples[i])
            last = i
    if len(samples) > 1:
        keys.append(samples[-1])
    return {"target": target, "keys": keys, "ease": "linear"}
