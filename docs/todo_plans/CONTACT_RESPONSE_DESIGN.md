# Contact response: concepts and ontology changes

Issue #36. This document is step one: name the concepts, state what the
ontology has to grow, and mark the points where a decision is owed.
No code yet.

Everything asserted here was read from the tree at `a5da3f8`, with
file:line. Where the issue text and the code disagreed, the code wins
and the correction is called out.

---

## 1. What is actually there

### Two rule systems, and they are not the same kind of thing

|  | capability response rules | `TransformationRule` |
|---|---|---|
| lives in | `src/capability/` | `src/interaction/` |
| declared as | KG properties `rule.N.trigger` on a part | a KG entity, `schema/logosphere.yaml:619` |
| triggers | 8 built-ins, open C++ registry | 3 values, closed parse |
| effects | 4 built-ins, open C++ registry | 4 values, closed enum |
| evaluated | pulled, inside `compute_from_kg` | pushed, once per episode |
| what it changes | a capability computation | a particle |

The issue framed this as "two disjoint half-systems" and asked whether a
contact rule joins one or becomes a third. That framing was wrong, and
correcting it is the main result of this pass.

They are disjoint because they are **two different kinds of rule**:

- **State rules.** A predicate over KG state, re-evaluated whenever the
  profile is recomputed, producing a modifier. `health_below:50` is not
  an occurrence, it is a condition that holds. Continuous. Idempotent.
  Order-free.
- **Event rules.** A reaction to something that happened, fired once,
  producing a mutation. `on_contact_filtered` is an occurrence. Discrete.
  Not idempotent. Ordered.

A contact response is an event rule. It has the shape of
`TransformationRule`, not of a capability rule. Which means there is no
third framework to invent and no merge to perform. There is one system
with the right shape whose trigger and effect sets were closed too early.

**The concept gap is not "a missing bridge". It is that the event-rule
system cannot name the event it reacts to.**

### The occurrent is never produced

`CollisionEvent` exists twice, and the two never meet.

- **Physics** `CollisionEvent`, `include/logosphere/physics/physics_system.h:65-77`.
  Pushed every frame at `src/core/physics_system_v4.cpp:972`. Carries
  normal, contact point, penetration, approach speed. Read by exactly two
  places, both in the animation layer: `humanoid_locomotion.cpp:216`
  (step climbing) and `:5555` (obstacle response).
- **Ontology** `CollisionEvent`, `schema/logosphere.yaml:649`, one slot,
  `collision_force`. Has a channel on the bus, `event_bus.h:25`.

`grep -rn "collisions()\." src/ include/ examples/` returns **six hits and
all six are in `tests/test_event_bus.cpp`**. The engine has never emitted
an ontological collision. The class is in the ontology, the channel is
wired, the journal would record it, and no instance has ever existed.

That is the real hole. Not a missing bridge between two working systems:
a missing **producer** for an occurrent the ontology already declares.

### Three defects found while reading

**`ImpactEvent` is dead in full.** Declared `physics_system.h:81`, member
`:513`, accessor `:353`. `grep -rn "impact_events_\|get_impact_events"`
across `src/ include/ examples/ tests/` returns those three lines and
nothing else. Never pushed to, never read. #36 proposed the bridge consume
`CollisionEvent`/`ImpactEvent`; `ImpactEvent` is not a source of anything.
Impact speed has to come from `CollisionEvent.relative_velocity`, which is
computed and correct.

**`a_is_dynamics` is misnamed.** The comment at `physics_system.h:74`
reads "Is particle_a dynamics-controlled?". The value written at
`physics_system_v4.cpp:977` is `pi.solver_mode == ParticleSolverMode::KINEMATIC`.
The field says dynamics and holds kinematic. Zero readers, so nothing
behaves wrongly today, but any consumer written against the name would
invert its logic. Rename before the first consumer, not after.

**The reverse edge is indexed and unreachable.** `KGCore` maintains
`incoming_relations` (`kg_core.h:159`), appended at `kg_core.cpp:485`,
cleaned at `:500`, memory-accounted at `:459`. Nothing queries it. There
is no `getRelatedReverse` on `KGModule` or `KGCore`.

This matters more than it looks. A contact arrives as two particle
indices. Getting from there to "the predator bit the prey" is:

```
render index  →  getKGParticleByRenderIndex   →  KGParticleID
              →  getEntityByKGParticle        →  the body part entity
              →  ???                          →  the creature
```

The last hop is the unexposed reverse `HAS_PART`. The index is already
built and already correct. It needs an accessor, not new bookkeeping.

### Confirmed from the issue, unchanged

- `TriggerRegistry::evaluate` is called from two sites, `capability_profile.cpp:170`
  and `:229`, both inside `compute_from_kg`.
- `TriggerContext` is `{part_id, entity_id, kg, health_ratio}`
  (`trigger_registry.h:45-50`). A trigger cannot see a contact at all.
- `grep -c "rule.0.trigger\|rule_group\|ResponseRule" schema/logosphere.yaml`
  returns 0. Rules are not in the ontology.
- Caps of 8 groups / 8 rules / 4 effects, each breaking on the first gap
  (`capability_profile.cpp:120, :163, :178, :223`).
- No impulse API exists anywhere in physics. Velocity is written directly
  through a `WriteView`, the way `apply_volume_forces` does it.

---

## 2. The concepts to add

### C1. A contact is an occurrent, and it must carry the contact

An event rule can only be as specific as the event it reads. Today the
ontological `CollisionEvent` carries one float. Everything a consequence
needs is computed in physics and thrown away at the layer boundary.

Slots to add to `CollisionEvent`:

| slot | range | why |
|---|---|---|
| `normal_x/y/z` | float | direction of the push. Knockback is meaningless without it, and it must come from geometry, never a world-up guess |
| `contact_x/y/z` | float | where. Bleeding spawns there |
| `penetration` | float | overlap depth, metres |
| `approach_speed` | float | relative velocity along the normal, m/s, negative approaching. The energy term for absorption and for "hard enough to matter" |
| `source_part_id` | integer | which part struck |
| `target_part_id` | integer | which part was struck. Armour is per-part; so is bleeding |

`source_entity_id` and `target_entity_id` already exist on `WorldEvent`
and carry the two creatures.

Open detail: those two are `string` on `WorldEvent`, while `EntityID` is
integral. The part slots above are proposed as `integer`. Either the
existing pair is wrong or the new pair should match it. Worth settling
once rather than adding a third convention.

### C2. There is no such thing as a kind of collision

The stated intent is "trigger an action based on the type of collision".
Reading the examples back (bleeding, knockback, armour absorption), the
thing being discriminated is never a property of the contact. It is:

- **what touched what** (a fang, not a foot)
- **how hard** (`approach_speed`)
- **where** (`target_part_id`)

A contact has no intrinsic type. Its character is entirely the roles of
its participants plus its energy. So the ontology should **not** grow a
`ContactKind` enum. It should make the participants legible in the event
and let the trigger express a predicate over them.

This is a claim worth disagreeing with explicitly if it is wrong, because
every later decision follows from it.

The vocabulary for "what kind of thing am I, for interaction purposes"
already exists and is already KG-declared: `ParticleInteractionProfile.category_bit`
(`schema/logosphere.yaml:601`). `category_bit` means "what am I",
`collides_with_mask` means "who do I touch". Using the category as the
contact-side role is not an overload, it is the slot's actual meaning.

### C3. A rule should be an entity

`rule.N.trigger` as a property prefix costs three things, all verified:
the validator cannot see a rule, an LLM reading the schema cannot discover
the vocabulary, and a typo in a trigger name is a silent no-op
(`trigger_registry.cpp` returns false for an unknown name). The hard caps
and the break-on-first-gap truncation are artefacts of the same encoding.

`TransformationRule` is already a class and already loaded from the KG by
`load_rules_from_kg`. Contact rules should be declared the same way, which
means the ontology grows the trigger and effect **vocabulary**, not just
the slots.

### C4. Solver authority decides what a bounce is

The invariant: KINEMATIC means an external writer owns position, and
`inv_mass` is 0, so the solver applies zero impulse. Verified by test in
`test_predator_hunt.cpp`: two overlapping KINEMATIC bodies produce a
correct `CollisionEvent` and 0 m of movement in 30 frames.

So "collide and bounce" between two kinematic creatures cannot be the
solver pushing them apart. Three ways out:

1. Creatures become DYNAMIC. Large blast radius, lands in the other
   session's physics and animation territory.
2. The effect writes a pending impulse; whoever owns the position drains
   it and decides what to do. Consistent with the invariant, because
   KINEMATIC already means "your owner moves you", so the push is
   delivered to the owner.
3. The engine writes velocity onto a KINEMATIC particle directly. Breaks
   the authority contract and locomotion would overwrite it next frame.

Only (2) holds the invariant. It also means the engine ships the
mechanism (deliver the impulse) and the game ships the policy (what a
push does to a walking creature), which is where the boundary is
supposed to fall.

---

## 3. Proposed ontology changes

Not a merge proposal. This is what the shape looks like if the concepts
above are accepted.

### Enums

`TransformationRule.trigger` and `.effect` are `string` today
(`schema/logosphere.yaml:913, :916`) with the permissible values listed
only in a description. The parse is closed in C++ and a bad string warns
and skips the rule.

Making them real enums gives the validator and any schema reader the
vocabulary. Extending the trigger enum:

```
ON_CONTACT          rigid contact occurred        (new)
ON_CONTACT_FILTERED pair declined rigid contact   (exists)
ON_VOLUME_ENTER                                   (exists)
ON_TIMER                                          (exists)
```

### Event rules become extensible

The closed effect enum is what forces a "third framework" if left alone.
Bleeding, knockback and absorption cannot be four enum values; they are
game policy. The registry pattern already proven for capability triggers
and effects is the answer, with a context that carries the contact:

```
ContactContext { normal, contact_point, penetration, approach_speed,
                 entity_a, entity_b, part_a, part_b,
                 type_a, type_b, kg }
```

Engine-provided triggers stay small and generic:

```
on_contact_with_type:<EntityType>   the other side resolves to this type
impact_above:<m/s>                  approach speed exceeds a threshold
```

Per D2 the types come from the KG, so `type_a` / `type_b` are the output
of the resolution chain and the trigger is a string compare against the
ontology type name. A game wanting finer discrimination registers its own
trigger and reads the entities out of the context directly.

Engine-provided effects stay smaller. `knockback:<speed>` is the only one
needed for the slice, and it writes a pending impulse per C4. Everything
named in the intent (bleeding, armour absorption) is game-registered, and
the engine's job is to make that registration possible, not to ship a
combat model.

### CollisionEvent grows the slots in C1

Then something has to emit it. That producer is the missing piece, and it
is new engine code, not a schema change.

---

## 4. Decisions taken

Settled 2026-08-02. Recorded with the reasoning that moved them, so a
later reader can tell a decision from a default.

**D1. A contact response is an event rule, extending `TransformationRule`.
Capability rules are untouched.** The push/pull split in §1 is real and
load-bearing: the push path consumes a queue of occurrences, fires once,
and clears it; the pull path scans state and is safe to run twice, which
it does. Contact belongs to the first. No third framework, no merge.

**D2. The contact-side role is the entity type from the KG, not
`category_bit`.**

This overrode the recommendation in the first draft of this document, and
the draft's supporting argument was wrong on the facts. It claimed
category bits were "a scarce 32-bit resource". They are not: the only
`.category =` assignment in shipping code is `butterfly_flight.cpp:107`,
bit 6. Thirty-one bits are unspent.

The real objection is coupling, not scarcity. `category` and
`collides_with` are the pair the broad phase consults:

```
contact(a, b)  <=>  (cat_a & mask_b) && (cat_b & mask_a)
```

Labelling a creature so a rule can match it would put that bit into
physics filtering for every pair it meets. Setting a category is not a
neutral act. Entity-type matching cannot perturb physics at all, and that
inertness is worth its cost.

The cost is real and lands in §5 step 3: resolution runs per contact per
frame, and the last hop does not exist yet.

```
render index → getKGParticleByRenderIndex → KGParticleID
             → getEntityByKGParticle      → body part entity
             → reverse HAS_PART           → the creature      (new)
             → getProperty(type)
```

Two consequences the producer must respect. First, resolution is skipped
entirely when no rule declares a contact trigger, the same fail-fast
`tick_transformations` already applies with `if (!rules_.empty())`.
Second, contacts are not rare: a humanoid standing on a tiled floor
generates them continuously, so the chain needs a per-frame cache and a
measured cost before it ships.

**D3. The bounce goes through a pending-impulse inbox.** The effect
writes an impulse; whoever owns the position drains it. The only option
that keeps solver authority intact, since KINEMATIC already means "your
owner moves you".

Two halves, and only the first is in this slice:

- *Engine.* Deliver the impulse. Small. The drain in the hunt test is
  `test_predator_hunt.cpp:657`, `move_particle(predator, px, py)`, fed by
  the `step_to` collide-and-slide at `:370`.
- *Locomotion.* A humanoid's hips are derived by the KinematicRoot model
  from a pinned stance foot. A push has to enter that model somewhere
  principled, not by writing hip x/y behind its back. Other session's
  territory. Not here.

Against making creatures DYNAMIC instead, one measured number:
`butterfly_flight.cpp:96-100` records physics going from 0.07 ms to 30 ms
when four butterflies began colliding with a garden.

**D4. Hygiene riding along:** dead `ImpactEvent` deleted, the `CLAUDE.md`
pointer to `ARCHITECTURE.md` corrected, and the damage/non-humanoid
evaluation gap fixed.

`a_is_dynamics` is renamed, but **the rename is a stopgap and must carry a
loud TODO**. Making the name match the value is not the fix. The field is
a flattened pair of bools standing in for three-valued solver authority;
the honest fix is to carry `ParticleSolverMode` and drop the bools. That
is deferred, and the TODO exists so the next reader does not mistake a
truthful name for a solved problem.

---

## 5. Sequence

Dependencies only.

1. **Ontology.** Trigger and effect vocabularies as enums, `ON_CONTACT`
   added, `CollisionEvent` slots from C1. Regenerate. Blocks everything.
2. **KG.** Reverse relation accessor over the existing `incoming_relations`
   index, so a contact resolves to its creature. On the critical path
   under D2, not optional. Rules as entities.
3. **Event bus.** The producer: physics contacts become ontological
   `CollisionEvent`s with the participants resolved. Skipped entirely when
   no rule declares a contact trigger; cached per frame; cost measured
   before it ships. Rename `a_is_dynamics` here, with the D4 TODO, before
   it has a consumer.
4. **First trigger and effect.** `on_contact_with_type:<EntityType>` and
   `knockback:<speed>`, with predator and prey carrying KG types, and a
   test that shows them touch and separate.

Each step is guarded before the next starts, per `docs/testing_guidelines.md`:
measured values printed, a control that proves the check can fail, and the
negative case.

Controls this design owes, named now so they cannot be quietly skipped:

- **Step 3.** A contact between two entities of types no rule mentions
  must produce zero resolution work, not merely zero effects. Assert the
  skip, or the fail-fast is decorative.
- **Step 4.** A pair whose types do not match the trigger must not bounce.
  Without it, "they separated" is unfalsifiable: two overlapping bodies
  drifting apart for any reason would read as a pass.
- **Step 4.** Separation must be measured against the pre-contact
  trajectory, not against zero. Both creatures are already moving under
  their own steering, so "distance increased" proves nothing on its own.
