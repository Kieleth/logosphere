# Shadow System Reference

## There is no shadow feature

Logosphere never draws a shadow. There are no shadow maps, no
projected blobs, no darkening decals. The renderer computes where
light ARRIVES; where geometry occludes the rays, light does not
arrive, and that unlit space is the shadow. Everything below is the
machinery of light transport — shadows are its absence.

## How light (and therefore shadow) is computed

The engine uses a 3-pass deferred GPU rendering pipeline. Occlusion is resolved in Pass 2 (shadow rays).

```
Pass 1: G-Buffer     — rasterize particles to screen, store world_pos + normal + color per pixel
Pass 2: Shadow Rays   — for each lit pixel, cast ray toward light, check if blocked by geometry
Pass 3: Apply Lighting — combine base_color × light_intensity × shadow_result → final pixel
```

Shadow geometry comes from the same particles that render. Each particle generates 12 shadow triangles (6 faces × 2 triangles). These are stored in a BVH (bounding volume hierarchy) for efficient ray-triangle intersection on the GPU.

## Critical rule: NO backface culling on shadow triangles

Shadow triangles must include ALL faces of every particle. Never cull based on camera direction.

The shadow ray goes from a floor pixel toward the light source. The triangles that block this ray are the ones BETWEEN the floor and the light. For a thin vertical particle (like a humanoid body part), the front-facing triangles (facing the camera) are exactly the ones that block shadow rays going sideways to the light. Removing them removes the shadow.

**Bug history (2026-04):** A backface cull (`if (dot(normal, camera_forward) > 0) continue`) removed front-facing shadow triangles. Large cubes still cast shadows because their back/side faces were sufficient. Thin particles (0.06-0.25m humanoid body parts) lost all visible shadow. The bug was hidden for months because test scenes used large cubes.

**Location:** `src/rendering/render_pipeline.cpp`, shadow triangle generation loop (parallel path ~line 270, serial path ~line 380). The cull was removed. Do not re-add it.

## Floor particles

Floor tiles (HEAVY_STATIC material, thin, wide) generate shadow triangles like any other particle. This is correct. Do NOT add special filtering to exclude them.

Earlier during debugging, a filter was added to skip HEAVY_STATIC floor tiles from shadow generation. This caused the floor to self-shadow (blocking all light) when the filter was incorrectly applied. The filter was removed. Floor tiles cast shadows, and the shadow system handles them correctly.

## Particle owner types and shadows

`ParticleOwner::DYNAMICS` does NOT affect shadow casting. DYNAMICS and PHYSICS particles both generate shadow triangles identically. The shadow system does not check the owner field. Verified with A/B test: two identical particles, one STATIC and one DYNAMICS, both cast identical shadows.

## Floor stability

Floor particles must be created with:
```cpp
floor.SetMaterial(Materials::Type::HEAVY_STATIC);
floor.owner = ParticleOwner::STATIC;
floor.is_at_rest = true;
```

Without STATIC owner and is_at_rest, the floor bounces on the turtle boundary due to physics gravity, causing the entire scene to vibrate every frame. HEAVY_STATIC material alone does NOT prevent physics from acting on the particle.

## Light positioning

- Light particles render as visible white diamonds on screen. Position them away from objects to avoid visual overlap.
- For testing, use a "streetlight" pattern: thin pole particle + light at the top.
- Light at z=5-10m with 500K-1M strength provides good illumination for scenes within 30m.
- Inverse-square falloff means objects closer to the light are brighter. If one object appears white (overexposed) while others are correctly lit, it's closer to the light, not a bug.

## Penumbra mode

`PenumbraMode::BLOCKER_GRADIENT` causes visible flickering on small particles. This is a known issue with the penumbra post-process interacting with sub-pixel geometry. `PenumbraMode::NONE` produces hard shadows without flickering.

## Shadow distance culling

`SHADOW_CULL_RADIUS` (default 30m, increased to 100m for Eden) controls how far from the camera center shadows are computed. Beyond this radius + `SHADOW_FADE_DISTANCE`, the GPU shader fades lighting to black. For large outdoor scenes, increase these values.

## Temporal lighting

`USE_TEMPORAL_LIGHTING` distributes shadow computation across frames via checkerboard pattern. With `TEMPORAL_FRAME_COUNT=1` it's effectively disabled. Higher values reduce GPU cost but cause visible checkerboard artifacts on small objects.

## Testing shadows

The headless test harness uses CPU shadow rays. The interactive mode uses GPU shadow rays (Metal compute or Metal RT). A shadow test that passes in headless does NOT guarantee it works in interactive. When debugging shadow issues, always test in interactive mode (run Eden or use `--visual-test`).

Shadow triangle count is logged in `FRAME_SUBMIT`:
```
[FRAME_SUBMIT] Frame 60 | GPU Submit: 0.16ms | Triangles: 228 render, 432 shadow
```

If shadow count is exactly half of expected (N particles × 12), backface culling is active and must be removed.

## Checklist for shadow debugging

1. Check `FRAME_SUBMIT` log for shadow triangle count. Expected: particles × 12 (minus light sources).
2. If count is half expected: backface cull is active in render_pipeline.cpp.
3. If floor vibrates: check floor particle owner (must be STATIC) and is_at_rest (must be true).
4. If shadows flicker: check PenumbraMode and USE_TEMPORAL_LIGHTING settings.
5. If small particles have no shadow but large ones do: check for size-based filtering in the shadow generation loop.
6. If DYNAMICS particles have no shadow but STATIC do: something is filtering by owner (should not happen, owner is not checked).
7. Compare headless (CPU) vs interactive (GPU) results. They use different shadow paths.
