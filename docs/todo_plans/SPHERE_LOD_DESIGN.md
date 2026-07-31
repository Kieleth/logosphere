# Sphere LOD: design discussion, not yet built

_2026-07-30. Follows kit study S10, which found the icosphere rebuild starving
the GPU, and the LOD quality/cost measurement that came out of it. Nothing here
is implemented. This is the reasoning, including the parts that argue against
building it._

## The measurement this starts from

`tests/test_sphere_lod_quality.cpp` renders one fixed scene at every icosphere
level and reports pixel differences against the highest as reference. Cost is
from the falling-bodies vehicle at 4,634 bodies.

| LOD | tris/sphere | MLP median frame | render CPU | surfaces built | pixels changed | % of lit | mean Δ |
|---|---|---|---|---|---|---|---|
| 0 | 20 | 15.65 ms | 6.21 | 66,188 | 28,012 | 1.47% | 9.7 |
| 1 | 80 | 22.30 ms | 11.02 | 146,888 | 17,089 | 0.90% | 6.6 |
| 2 | 320 | 58.02 ms | 43.54 | 469,688 | 14,777 | 0.78% | 3.4 |
| 3 | 1280 | (reference) | | | 0 | 0.00% | 0.0 |

320 to 80 is 2.6x on the frame. Note that level 2, the shipped default, is
itself 0.78% off level 3: it is not converged either.

**What the images show.** Level 0 is an icosahedron with a polygonal shadow,
unusable. Level 1 is visibly faceted on the two largest spheres. From roughly
1.0 m and smaller on screen, level 1 and level 3 are indistinguishable. **The
difference is entirely a function of screen size**, which is what makes a single
global constant the wrong shape for this knob.

## The four proposals, judged

### Screen-size LOD — yes

The criterion is triangles per pixel. A sphere covering 20 pixels with 320
triangles pays 16 triangles per pixel to describe a shape the rasterizer
resolves in a handful. The inputs are already in the right place:
`collect_surfaces` computes `ppu = camera_system.get_pixels_per_unit()` and the
particle half-extent to size the frustum margin, so screen radius is free in the
exact loop that generates geometry.

An icosphere's silhouette has on the order of `sqrt(faces)` segments. For a
smooth outline you want segment length under roughly two pixels. At 100 px
radius that argues for more than level 2, which is exactly why level 2 still
differs from level 3. At 5 px radius it argues for level 0.

### FPS feedback — no

Rejected for a reason specific to this project: it makes rendering a function of
timing. The whole measurement methodology rests on byte-identical pixel oracles
and reproducible frames. S3's light-scaling result was destroyed by a confound
below the process boundary; MLP calls `wait_for_completion` every frame purely to
keep dumps deterministic. A feedback controller inside the thing being measured
confounds every A/B, and disabling it to measure means never measuring the
shipping build, which contradicts the amendment in `PERFORMANCE_RESEARCH.md`
principle 3.

Ordinary control problems apply too: quality affects FPS affects quality with a
lag, so without damping you get visible pumping; and it responds globally to
what is usually a local cause.

**If a global governor is wanted, make it a budget, not a feedback loop.**
"Total triangles this frame must fit in N" is deterministic given scene and
camera, so it re-derives the same answer every run and the oracles survive.
"React to last frame's time" does not.

### Motion-driven quality — no, but the instinct redirects

The usual justification is that motion blur hides detail. This engine has no
motion blur, so the premise is missing. Faceting behaves badly under motion
specifically: a rotating flat-shaded icosphere makes its facets swim as the
tessellation turns relative to the light. Static faceted reads as stylised;
moving faceted reads as broken. Tying quality to speed also means quality
changes on every acceleration, at moments the viewer cannot anticipate, unlike
distance which changes monotonically and can be hysteresis-damped.

**The redirect:** motion should decide whether geometry is REBUILT, not how good
it is. `USE_RENDER_SURFACE_CACHE` already exists from study S9, keyed on the
particle transform, and measured 94-99% hit rates in Eden. That is the version
of the idea that pays, and most of it is written.

Also rejected: promoting resting objects to a higher level. If the authored
target is level 2, going to 3 when an object stops means the scene visibly
changes when things come to rest. Automatic adjustment must be one-directional.

### Faceted as art direction — yes, and it reshapes the design

If faceted is sometimes the DESIRED look (a Dune-style shield reads better as an
icosahedron than as a smooth ball), then LOD is not a degradation axis, it is an
authored property. That forces the shape the architecture rules already want:

- **Engine mechanism:** reduce detail when screen size does not justify it.
- **Game policy:** this shield is an icosahedron at every distance; that marble
  is smooth.
- Per-particle authored target; automatic reduction may only go DOWN from it.
- "Faceted" is an appearance property, so it belongs in the schema, not in
  `optimization_flags.h`.

## Engine-specific traps

1. **LOD transitions defeat the BVH refit.** `prepare_gpu_data` keeps
   `particle_tri_start_` / `particle_tri_count_` for a dirty-refit path. If a
   particle's triangle count changes, refit is impossible and a full rebuild is
   forced. `prep_bvh` is 4.65 ms and an observed rebuild cost 9.47 ms. Hysteresis
   is therefore a PERFORMANCE requirement, not a visual nicety: without wide
   bands, a camera drifting across a threshold triggers full rebuilds every frame.

2. **The shadow path needs its own criterion.** Shadow triangles come from a
   separate `GetShadowTriangles` call. The right LOD for a shadow depends on the
   SHADOW's screen size, and a small object near a light casts a huge one. In the
   LOD 0 dump the shadow was a hard polygon while the sphere itself was only
   mildly wrong.

3. **The surface cache key must include the level**, or a particle that changed
   LOD reuses stale geometry.

4. **Popping** needs hysteresis bands (switch up at radius X, down at ~0.8X),
   which trap 1 requires anyway.

## The deeper question: why 320 triangles are needed at all

The renderer is **flat-shaded**. `Surface` carries a single normal,
`make_triangle_surface` fills it from the face normal, and `TriangleLit` has one
`float normal[3]` per triangle. There are no per-vertex normals anywhere. So the
only reason level 3 looks smooth is brute force: enough facets that the shading
discontinuities fall below a pixel.

Interpolated vertex normals, which for an icosphere are just the normalised
vertex positions and therefore free to compute, would decouple SHADING
smoothness from triangle count. Triangle count would then only have to satisfy
the SILHOUETTE, which needs far fewer. Level 1 with smooth normals would
plausibly look better than level 2 does today at a quarter of the cost, and the
icosahedron look stays available by keeping flat normals where they are wanted.

### Why this is not free: the shadow terminator

Raised by the owner, and it is the sharpest objection to the whole idea.

Smooth normals create a **shading normal / geometric normal split**. Shadow rays
intersect the real triangles. Near the terminator you get points whose
interpolated normal faces the light while the actual mesh occludes the ray
against a neighbouring facet of the same sphere. That is the classic shadow
terminator artifact: dark blotchy banding exactly where the sphere should roll
into shadow, and it is worst on coarse meshes, which is precisely where we would
be using it. Flat shading does not have the problem because a facet's normal and
its geometry agree by construction.

This lands on the pure-ray-shadow premise, which is the engine's differentiator
and not something to compromise. Known mitigations, none free:

- Clamp the visibility term with the GEOMETRIC normal (`dot(Ng, L) <= 0` forces
  shadow). Removes the wrong-side case, but reintroduces a faceted terminator.
  Honest trade: smooth interior, faceted terminator.
- Offset the shadow ray origin along a smoothed surface reconstruction
  (the Hanika / Cycles approach). More correct, more code in the ray path.
- Keep more triangles only where the terminator is, which needs the light
  direction at geometry-build time. Complicated and view-dependent.

It also touches more than direct lighting: SSDO, DDGI and the penumbra blocker
search all read normals, and `TriangleLit` would need a second normal (there is
padding to spare, but the raster kernel would have to interpolate).

**Verdict:** a bounded experiment with a named risk, not a plan. The first thing
to measure is not performance, it is whether the terminator artifact is visible
at level 1 on the LOD test scene.

## Open questions, unverified

- Does anything downstream assume one normal per triangle? The G-buffer kernel,
  SSDO and DDGI all read normals; per-vertex means interpolating in the raster
  kernel.
- Does the ontology have a slot for an appearance property like faceted vs
  smooth? It should, if faceting becomes authored.
- Is the Eden camera deterministic enough for oracles once geometry depends on
  camera distance? MLP's is; Eden's is not currently an oracle scene anyway.

## Proposed order, subject to the owner's call

1. Smooth-normals experiment, judged first on the terminator artifact, not on
   perf. It may change which LOD levels are acceptable at all, so it comes first.
2. Screen-size LOD with hysteresis and a per-particle authored cap.
3. A triangle budget only if 1 and 2 leave a gap.

Motion drives caching, not quality. FPS feedback not at all.
