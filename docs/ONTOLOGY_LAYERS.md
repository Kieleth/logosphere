# Ontology layers

The type system comes in three layers. Each one knows less about your
game than the one above it, and the engine knows nothing about any of
them beyond the first.

```
  game extension     examples/<game>/schema/<game>.yaml
        │            what this game alone means: seeds, rules, roles
        ▼
  setting pack       schema/packs/<pack>.yaml
        │            what a KIND of world contains: sky, soil, life
        ▼
  universe core      schema/logosphere.yaml
                     what the ENGINE understands: bodies, materials,
                     joints, events, solver authority
```

## Why the split exists

The core is the vocabulary the engine itself acts on. Physics reads
solver authority. The damage system reads tissue and health. The event
bus carries `WorldEvent` and its descendants. Nothing in `src/`
mentions a tree, a planet, or a snake — verifiably: the engine's only
uses of generated ontology types are `WorldEvent`, `DamageType`, and
the event family.

Everything else is a setting. A world does not inherently have
astronomy any more than it inherently has trees. Those are choices a
game makes, and the ontology should say so rather than handing every
game the same fixed universe.

There is a second reason, and it is the one that earned the work. The
ontology is where engine knowledge becomes unavoidable. An agent — a
model, or a person reading the schema — can only reach for what the
vocabulary offers. Make the wrong lever unavailable and the wrong
choice becomes unmakeable. See `solver_authority`, which exists
precisely so that nothing is ever again held up by `is_at_rest`.

## Loading a pack

A pack is inert until asked for. Compiling one is not loading it.

Declare it in the importing schema:

```yaml
imports:
  - linkml:types
  - logosphere
  - space
```

The generator flattens imports, so the game's own registry already
carries its whole closure, and the usual one-liner is enough:

```cpp
kg.extendOntology(mygame::ontology::registry());
```

To add a setting to a world that is already running — because a human
asked for a sky mid-session — extend with the pack directly:

```cpp
kg.extendOntology(space::ontology::registry());
```

## What happens when a pack is missing

`createEntity` rejects unknown types, loudly:

```
[KG] REJECTED: unknown entity type 'Planet' (not in ontology)
```

This is deliberate and is the half of the contract that makes layering
real rather than decorative. A pack that half-works when absent is
documentation, not structure. `tests/test_ontology_packs.cpp` holds
both directions: the vocabulary is present once loaded, and absent
otherwise.

Note the consequence for engine-shipped generators. `PlanetGenerator`
lives in `src/worldgen/` but creates a `Planet`, which lives in the
space pack; `TreeGenerator` will be in the same position once an
earth-like pack exists. The generator is a tool the engine ships; the
pack is the vocabulary that makes its output nameable. Using the tool
requires loading the pack, and forgetting to says so immediately.

## Writing a pack

Add `schema/packs/<name>.yaml`, importing `logosphere`. The generator
discovers it, produces `src/generated/<name>_ontology.{h,cpp}` in
namespace `<name>::ontology`, and stages it so any schema importing it
resolves. Register the generated registry in `CMakeLists.txt` beside
the core one.

A type belongs in a pack, not the core, when the engine would not
notice its absence. If `src/` never names it and no engine system
reads its slots, it is a setting.

## Current state

- **Core** — bodies, anatomy, materials, joints, capabilities, damage,
  events, constraints, interaction profiles, solver authority, and the
  abstract bases everything hangs from (`LivingEntity`, `Creature`,
  `NaturalFormation`, `Structure`, `Floor`).
- **`space`** — `CelestialBody`, `Sky`, `Planet`, orbital slots, and
  the `CelestialKind` vocabulary.
- **`earth`** — `Plant`, `Tree`, `PhysicsTree`, `Grass`, `GrassPatch`,
  `Branch`, `Leaves`, `FallenTree`, `Rock`, `PhysicsRock`, `Snake`,
  `Butterfly`, `Totem`, with `TreeSpecies`, `OrganicType`, `LogType`
  and `RockSize`.

Which games ask for what is now a real signal rather than an
assumption: Eden and Logogenesis import `earth` (Logogenesis also
`space`); **Logotron imports neither**, because a game of light-cycles
in an arena has no botany and no sky. That is the layering earning its
keep — before this, every game carried the whole world.

## Migrating a type out of the core

Moving `Tree` out broke five tests, and that was the mechanism
working. Each was creating earth vocabulary against a core-only
registry, and one of them — the shared `TestContext` — had been
getting `INVALID_ENTITY` back and passing anyway, because nothing
checked. A silent failure that a layering change turns into a loud
one is the argument for doing this at all.

The fix in every case was one line: ask for the pack.

```cpp
kg.extendOntology(earth::ontology::registry());
```

Expect that. If a test breaks when you move a type, it is telling you
which layer it actually belongs to.
