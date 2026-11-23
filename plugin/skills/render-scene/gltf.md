# glTF models

Part of the scene reference: the core (structure, objects, orientation, facts, gotchas) is in `reference.md`.

`"mesh": "models/chair.glb"` (or `.gltf`, path relative to the scene file) imports the whole file as one static mesh.

- **Axes:** glTF is Y-up, the engine is Z-up. A model comes in with glTF +Y → up (+Z), glTF +Z (its front) → +X (the engine's forward), glTF +X → −Y. So a glTF character facing +Z faces +X here, like the rest of this format.
- **No recentering or rescaling:** units stay meters and the origin stays where it was modeled. Use `pivot` (`"bottom"` or `"center"`) to rest it on the floor or spin it around its middle, and `scale` to size it.
- **Materials:** base color (factor and texture), metallic/roughness (factors and texture), normal map, occlusion, emissive (factor, texture, `KHR_materials_emissive_strength`), `KHR_materials_unlit`, vertex colors, alpha mask cutouts and double-sided. Textures can be embedded (`.glb`, data URIs) or image files next to the model. `KHR_texture_transform` is ignored. No skinning or animations (animate the object instead), no cameras or lights from the file.
- `material` or `color` on the object replaces the model's materials with one of ours.
- Prefer textures with a plain `LINEAR` min filter (9729); some drivers have rendered mipmapped samplers black.

A minimal textured model is in `examples/assets/crate.gltf` (a 1 m cube resting on its own origin), used by `examples/textured.json`.
- glTF meshes also work as an emitter's instance mesh (see emitters.md).
