# Eden, The Knowledge Garden

**The Logosphere knowledge-graph showcase.**

A mythological tableau that exercises the KG and capability stack end
to end: Eva and Adam walk a streamed, layered garden among trees,
apples, a lamp, and the serpent. All of them are particles, and every
particle is a node in the knowledge graph. The scene doubles as a
living integration test for the ontology layer.

![Eden](../../assets/screenshots/eden.png)

## Run it

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build -j 8
./build/eden/eden
```

Move Eva with WASD; the mouse steers her gaze (the yaw cascade turns
eyes, then head, then torso, then hips, the way a person turns). Walk
to the tree; take the apple; see what changes.

## What it demonstrates

- **Entities as typed KG nodes.** Eva, Adam, the Tree, the Apple, and
  the Serpent are ontology-typed entities with HAS_PART structure,
  properties, and relations, created through the same schema-validated
  path a game would use.
- **Full humanoid rigs.** Physics-based bodies (gluon-constrained
  particles), kinematic-root locomotion, foot planting, and look-at
  with the biomechanical yaw cascade; face features ride the head.
- **Streamed layered terrain.** The garden floor is chunk-streamed
  strata (bonded bedrock, sediment, organic top), not a static plane;
  chunks load and unload around the walker while particle indices
  stay coherent.
- **Capability and consequence.** Interactions flow through the
  capability system and KG relations rather than scripted triggers:
  relations like `Tree bears Apple` and `Serpent tempts Eva` are
  queryable state the game reads and reacts to.
- **Real lighting.** Metal-compute shadow rays, soft shadows, and GI
  over the whole tableau; the lamp and the sun are light-emitting
  particles.

## Where to look

- `examples/eden/src/main.cpp`, the whole game: engine setup, scene
  construction, input, and the KG wiring. Written as the reference
  integration pattern for `IApplication`.
- `examples/eden/schema/eden.yaml`, Eden's ontology extension on top
  of the engine's base schema.
- [docs/GAME_LAYER.md](../../docs/GAME_LAYER.md), the API surface
  Eden consumes, documented.

## Design rule

Eden is a CONSUMER of Logosphere: it uses engine mechanisms and never
duplicates them. Anything Eden needed that the engine lacked was added
to the engine first, which is exactly the contribution pattern the
engine expects from games built on it.
