"""Snowy village fly-through (6 s): a scene written as a Python script with cerender_lib.

Houses are built from parts at scatter() spots along a road, pines are a static emitter that keeps clear
of the houses and the road, snow is a falling emitter, and the camera flies down the road with look_along().
Render it like a .json scene: python cerender.py render village.py --preview
"""

from cerender_lib import Scene, look_along, rng, scatter

s = Scene(duration=6, width=1080, height=1350)
s.environment(sky=["#9dbde3", "#eef3f8", "#e4ebf2"], ground="#eef2f7", ambient=0.55, fog={"start": 14, "end": 45})
s.material("snow", color="#f7f9fc", roughness=0.9)
s.material("door", color="#5a3d34")
s.material("flake", color="#ffffff", unlit=True)

s.add("road", mesh="cube", color="#cfd5de", position=[0, 0, 0.01], scale=[60, 2.6, 0.02], shadow=False)
road = {"min": [-30, -3.2], "max": [30, 3.2]}   # the road plus room for a house's half depth

# Houses on both sides of the road, facing it; each gets its own size and wall color
r = rng(5)
walls = ["#e8a87c", "#d87a6a", "#f2d07e", "#9fc4a8", "#a7b8e0"]
spots = scatter(12, {"min": [-14, -6.5, 0], "max": [14, 6.5, 0]}, min_gap=3.6, avoid=[road], seed=8)
for i, (x, y, _) in enumerate(spots):
    w, d, h = r.uniform(1.6, 2.4), r.uniform(1.6, 2.2), r.uniform(1.1, 1.6)
    s.group(f"house{i}", position=[x, y, 0], rotation=[0, -90 if y > 0 else 90, r.uniform(-8, 8)])
    s.add(f"walls{i}", parent=f"house{i}", mesh="cube", color=r.choice(walls), pivot="bottom", scale=[w, d, h])
    # A 3-sided cylinder tipped onto its side is a gable roof, ridge running front to back
    s.add(f"roof{i}", parent=f"house{i}", mesh="cylinder", detail=3, flat=True, material="snow",
          position=[0, 0, h + 0.5], rotation=[90, 0, 0], scale=[1.35, (d + 0.3) / 0.866, w + 0.3])
    s.add(f"door{i}", parent=f"house{i}", mesh="cube", material="door", pivot="bottom",
          position=[w / 2 + 0.03, 0, 0], scale=[0.06, 0.45, 0.8])

# Pines: one emitter, kept off the road and away from every house
s.emitter("pines", "cone", 160, {"center": [0, 0, 0], "radius": 22, "inner": 2}, detail=6, flat=True, pivot="bottom",
          color=["#3f7a5a", "#4d8a63", "#36694f"], size=[0.8, 1.4], scale=[0.9, 0.9, 2.2], rotation="yaw", min_gap=1.4,
          avoid=[road] + [{"center": [x, y], "radius": 2.4} for x, y, _ in spots], seed=2, shadow=True)

s.emitter("flakes", "sphere", 900, {"min": [-12, -6, 0], "max": [12, 6, 7]}, material="flake",
          size=[0.03, 0.06], motion={"fall": 0.9, "drift": [0.3, 0.2, 0], "sway": {"amplitude": 0.25, "period": 3}}, seed=4)

s.light("sun", intensity=1.0, color="#fff4e6", position=[3, -6, 6])
s.camera("cam")
s.animate(look_along("cam", [[-14, -0.6, 2.8], [-9, 0.5, 2.4], [-4, -0.4, 2.3], [1, 0.4, 2.5], [6, 0, 2.8]], duration=6))
s.write()
