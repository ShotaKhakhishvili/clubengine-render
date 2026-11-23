# Animation tracks

Part of the scene reference: the core (structure, objects, orientation, facts, gotchas) is in `reference.md`.

Each track sets one property of one object over time. `target` is `"id.property"` or `"id.property.x|y|z"` for one component.

Properties: `position`, `rotation`, `scale` (vectors), `color` (meshes and lights), `intensity` (lights), `visible` (true/false), `lookAt` (a point; replaces an object target).

- **`visible` keys are eased like 0/1 numbers and flip at 0.5**: with the default ease the switch happens halfway between two keys. Put `"step"` on the key before the switch (`[[0, true, "step"], [2, false]]` hides at exactly 2 s) or set `"ease": "step"` on the track.
- **`lookAt` keys** (or `path`, `follow`, `wave` on `lookAt`) aim at a point. That replaces an object target, starting from where that object was. A look-at sets the world pitch and yaw (ignoring `rotation` and the parent's turn) and keeps `rotation`'s roll.
- A `color` track changes a mesh's base color, not its glow (see reference.md, Facts & limits). Emitters reject `color` tracks.

Each track has exactly one of `keys`, `spin`, `wave`, `orbit` (basics) or `follow`, `path`, `bounce`, `walk` (motion helpers):

**keys**: values at times, eased in between. A keyed track does nothing before its first key and holds its last value after its last key. So you can split a story into beats: several keyed tracks on the same property, each starting where the previous one ends (start each one with the value the previous one ended on).

```json
{ "target": "ball.position.z", "keys": [[0, 0.5], [0.4, 2.5, "inQuad"], [0.8, 0.5]], "ease": "outQuad" }
```

Each key is `[time, value]` or `[time, value, "ease"]`; the ease shapes the segment that *starts* at that key, and `"ease"` on the track is the default (`inOutCubic` if unset). Vector values can also be written flat, `[time, x, y, z]` or `[time, x, y, z, "ease"]` (same as `[time, [x, y, z]]`): the compact form generated tracks use.

**Repeating keys:** `"repeat": true` plays the key range (first to last key) over and over, from the first key on. `"period": seconds` repeats it every `period` seconds instead, holding the last value in between (blink every 2 s: keys over 0.2 s, `"period": 2`). For a seamless loop make the last value equal the first.

**Phase offset:** there's no option for it. Start the key range earlier instead: a track runs from its first key, so keys at `[[-0.5, a], [0, b], [0.5, a]]` with `"repeat": true` play half a 1 s cycle ahead of the same keys at 0–1. Use it to desynchronize copies (lamps, soldiers, waves). `wave` has `phase` for this.

```json
{ "target": "tip.scale", "keys": [[1, 0.1], [1.1, 0.16], [1.3, 0.1]], "period": 1 }
```

Eases: `linear`, `step`, `inQuad`, `outQuad`, `inOutQuad`, `inCubic`, `outCubic`, `inOutCubic`, `inSine`, `outSine`, `inOutSine`, `inBack`, `outBack`, `inOutBack`, `outBounce`, `outElastic`. `outBack` and `outElastic` overshoot, which is great for pop-in scale animations.

**spin**: adds a constant rotation speed in degrees per second, `[pitch, yaw, roll]`.

```json
{ "target": "box.rotation", "spin": [0, 90, 0] }
```

**wave**: adds `amplitude × sin(2π × (t / period + phase))` on top of the value. Good for bobbing, breathing, pulsing.

```json
{ "target": "ufo.position.z", "wave": { "amplitude": 0.2, "period": 1.5 } }
{ "target": "heart.scale", "wave": { "amplitude": 0.08, "period": 0.6 } }
```

**orbit**: moves a position on a horizontal circle. Perfect for camera turntables (combine with `lookAt`).

```json
{ "target": "cam.position", "orbit": { "center": [0, 0, 0], "radius": 4, "height": 2, "period": 8, "startAngle": -120 } }
```

`period` is seconds per full turn (negative turns the other way); `startAngle` is in degrees around Z, 0 = on the +X side.

**follow**: sets a `position` or `lookAt` to another object's world position plus an `offset` (world axes). `smoothing` (seconds, default 0) makes it trail behind like a camera operator; 0.3–0.6 feels natural. Tracking shot:

```json
{ "target": "cam.position", "follow": { "object": "bot", "offset": [0.5, 1.6, 1.1], "smoothing": 0.5 } },
{ "target": "cam.lookAt",   "follow": { "object": "bot", "offset": [0, 0, 0.7], "smoothing": 0.25 } }
```

**path**: moves a `position` (or a `lookAt` point) through points at an even speed.

```json
{ "target": "car.position", "path": { "points": [[2, 0, 0], [0, 2, 0], [-2, 0, 0], [0, -2, 0]], "loop": true, "duration": 6, "face": true } }
```

| key | default | notes |
|---|---|---|
| `points` | required | `[[x, y, z], ...]`, at least 2 |
| `curve` | `smooth` | `smooth` passes through every point, `linear` goes straight between them, `bezier` takes `point, handle, handle, point, ...` (3n + 1 points; 3n when looping) |
| `start`, `duration` | 0, to the end of the clip | before `start` the object waits at the first point, after the end it stays at the last |
| `loop` | false | closes the path and goes around again every `duration` |
| `face` | false | `position` paths: turns the object (yaw) along its direction of travel. It adds to the object's own yaw, so use `rotation` to correct a model that doesn't face +X |
| `ease` | `linear` | over the whole trip (`inOutSine` starts and stops gently) |

**bounce**: hops added on top of a `position.z` (its resting height). Use `"pivot": "bottom"` on the object so the squash stays on the floor.

```json
{ "target": "ball.position.z", "bounce": { "height": 1.5, "period": 0.8, "squash": 0.25, "restitution": 0.7 } }
```

| key | default | notes |
|---|---|---|
| `height` | 1 | top of the first hop, above the resting height |
| `period` | 1 | seconds per hop |
| `restitution` | 1 | 1 hops forever (loops); below 1 each hop is lower and shorter until it rests (0.6–0.8 looks real) |
| `squash` | 0 | squash at impact, stretch while fast, round at the top (0.15–0.3); also scales the object |
| `start` | 0 | time of the first ground contact; before it the object rests. Use `-period / 2` to start at the top |

**walk**: a walk cycle on a rig of joint groups (see `examples/character.json`). The target is the rig's root id alone. Joints are pairs, left then right; any can be left out.

```json
{ "target": "bot", "walk": { "legs": ["hipL", "hipR"], "knees": ["kneeL", "kneeR"], "feet": ["ankleL", "ankleR"],
                             "arms": ["shoulderL", "shoulderR"], "period": 1.1, "swing": 22, "start": 0, "end": 3 } }
```

| key | default | notes |
|---|---|---|
| `legs`, `knees`, `feet`, `arms` | | joint ids. Legs swing with pitch, knees bend while their leg swings forward, feet stay level, arms swing opposite to the legs |
| `period` | 1 | seconds per full stride (left and right step) |
| `swing`, `kneeBend`, `armSwing` | 25, 40, 20 | degrees |
| `bob` | 0.03 | how far the root dips at full stride; about leg length × (1 − cos swing) keeps the feet on the floor |
| `start`, `end` | 0, end of clip | eases in and out over 0.25 s |

The walk doesn't move the character: pair it with a `path` (with `face`) on the root. To keep feet from sliding, travel about 4 × leg length × sin(swing) per period.

Tracks apply in order: `keys`, `orbit`, `follow` and `path` set the value, `spin`, `wave`, `bounce` and `walk` add to whatever is there, so a `wave` after a `keys` track bobs around the keyed motion. Put `walk` after any keyed tracks on the same joints.
