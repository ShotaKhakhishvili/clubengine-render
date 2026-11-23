# Camera

Part of the scene reference: the core (structure, objects, orientation, facts, gotchas) is in `reference.md`.

Exactly one object with `"camera": {}`. Aim it with `lookAt`. The field of view is fixed at **90° vertical**, which is wide: to fill most of the frame with an object of height `h`, keep the camera about `0.8 × h` to `1.2 × h` away from it. A camera slightly above the subject looking down 15–30° looks best.

- `lookAt` can be an object id (tracks it every frame) or a point. Keying `lookAt` (or `path`, `follow`, `wave` on it) aims at a point; see tracks.md. A look-at sets the camera's world pitch and yaw and keeps `rotation`'s roll, so a roll track still banks the camera.
- Moves that look premium: `orbit` turntables, slow push-ins (keys on `position`), `follow` tracking shots (tracks.md), `look_along` fly-throughs from `cerender_lib` (python-lib.md).
- The wide 90° field of view makes things look smaller than you expect: get close. Distant flying objects need to be big or close to read.
- **Check clearance.** The camera has no collision: on a path through a city or a crowd it can pass inside walls or graze objects. Render consecutive frames (`--range`) along the move and keep it at least a meter from geometry.
