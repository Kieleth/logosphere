# Observing change: the event bus and `PlannedWorld`

How to find out that the world changed, or is about to.

Logosphere gives you two mechanisms, and they answer different
questions. Picking the wrong one produces code that is silently,
intermittently wrong, so this page starts with the choice and then
explains each.

---

## Pick one

| You want to… | Use | Why |
|---|---|---|
| React after something happened (spawn a corpse, play a sound, update AI) | **Event bus**, `subscribe()` | Fires the moment the change is real |
| Catch up on everything since you last looked (LLM director, replay, analytics) | **Event bus**, `create_reader()` + `drain()` | Cursor-based journal, no polling, loss is counted not hidden |
| Ask what the world looks like right now | **`KGModule`** directly | It is the committed truth |
| Read a value your own in-flight rule has already changed | **`PlannedWorld`** | The graph and the bus both still describe the world as it was before your rule started |

**The trap in one sentence:** during a rule's own execution, the graph
has not changed yet and no event has fired yet, so a handler that reads
`context.kg` to modify a value a previous step of the same rule already
modified will silently clobber it.

---

## Part 1: the event bus

`include/logosphere/events/`. Two tiers over the same emit: synchronous
signals for immediate reaction, and a retained ring journal for
consumers that think in "what happened since".

### Subscribing (tier 1: signals)

Runs immediately, inside the emit. Use it for invariant maintenance and
reactions.

```cpp
auto id = engine.get_event_bus().damage().subscribe(
    [](const onto::DamageEvent& e) {
        // runs now, on the emitting thread
    });
```

### Reading the journal (tier 2: history)

Hold **one reader per consumer for its lifetime** and drain at your own
pace. The cursor makes every drain mean "everything since I last
looked", so seconds or minutes between drains is fine.

```cpp
auto reader = bus.deaths().create_reader();   // once, at init
// ... later, whenever you like ...
for (const auto& ev : reader.drain()) { ... }
reader.dropped_count();   // events lost to capacity eviction, never silent
```

`create_reader()` starts at the head: events already journaled are not
re-delivered. For history from before your consumer existed, use the
stateless `collect_since(seq, max_n)`.

Retention is capacity-based, 1024 events per channel by default
(`set_capacity`). A reader that falls behind a full ring skips forward
and counts the loss. It never blocks and never lies.

### Channels

| Channel | Type | Carries |
|---|---|---|
| `collisions()` | `CollisionEvent` | physics collisions |
| `damage()` | `DamageEvent` | HP changes |
| `spawns()` | `SpawnEvent` | entity creation |
| `deaths()` | `DeathEvent` | something the game considers alive stopped being alive |
| `perception()` | `PerceptionEvent` | AI sense events |
| `state_changes()` | `WorldEvent` | KG property mutations, with `{property, value, prev}` |
| `relations()` | `RelationEvent` | HAS_PART and friends created/removed |
| `dice_rolls()` | `DiceRollEvent` | engine-side rolls, as citable facts |
| `transformations()` | `TransformationEvent` | a TransformationRule fired |
| `volume()` | `VolumeEvent` | medium entry/exit |
| `contact_filtered()` | `ContactFilteredEvent` | pair excluded from narrow phase |

`deaths()` means deaths. `kg.destroyEntity` is bookkeeping and emits
nothing: a recycled trail segment is not a death.

### Automatic KG emission

Wire a bus into the graph with `kg.set_event_bus(&bus)` and mutations
announce themselves:

- `setProperty()` → `state_changes` (skipped when the value is unchanged)
- `createRelation()` / `destroyRelation()` → `relations`

### The guarantee: events only ever speak about facts

**Nothing is emitted for a change that might be rolled back, and
nothing is emitted part-way through a batch.**

`apply_kg_ops_atomically` (`src/kg/kg_ops_transaction.cpp`) detaches the
bus for the duration of a batch, buffers every event the mutations
would have raised, and re-emits them only after the whole batch has
committed. A batch that fails half-way undoes its writes and emits
nothing at all.

That is deliberate. A subscriber that saw a change which was later
rolled back would be holding a fact the world never contained.

The consequence is the important part:

- **Reacting to what happened: yes.** By the time your subscriber runs,
  the change is real and the graph agrees with it.
- **Watching a change in progress: no.** Code running *inside* a batch,
  before the commit, learns nothing from the bus, and learns nothing
  from querying the graph either. Both still describe the world as it
  was before the batch began.

That second case is what Part 2 is for.

---

## Part 2: `PlannedWorld`

`include/logosphere/rules/planned_world.h`.

A read view over **the world as the current plan will leave it**:
committed graph, plus the ops the in-flight rule has planned but not
yet applied.

### The problem it solves

A typed outcome resolves into an ordered list of KG ops applied
atomically at the very end. Some rules change the same thing twice.
Cepheus aging, for example:

> "Reduce two physical characteristics by 2, reduce one physical
> characteristic by 1"

That is two changes over one group of three, and the book means all
three characteristics go, each exactly once. The second change has to
know what the first one took. It cannot ask the graph (unchanged until
commit) and it cannot subscribe (nothing emitted until commit).

### Using it

Every outcome handler receives one on its context:

```cpp
executor.register_handler("Bleed",
    [](const OutcomeHandlerContext& ctx, OutcomePlan& plan,
       std::string& error) {
        // RIGHT: sees what earlier steps of THIS rule already did.
        const kg::EntityRef target{ctx.target, ""};
        const std::string now = ctx.planned.property(target, "endurance");

        // WRONG: ctx.kg is the committed graph. It still reads as it
        // did before this rule started, so this would silently
        // discard an earlier step's change.
        // const std::string now = ctx.kg.getProperty(ctx.target, "endurance");

        plan.ops.emplace_back(kg::KGOpSetProperty{
            target, "endurance", std::to_string(std::stoi(now) - 1)});
        return true;
    }, error);
```

**Rule of thumb: any handler that reads a value, changes it, and writes
it back must read through `ctx.planned`.** Handlers that only append
new facts can ignore it.

### The five questions it answers

```cpp
std::string property(ref, key)     // newest planned value, else committed
bool        has_property(ref, key)
std::string type_of(ref)           // committed type, or a pending create's
bool        exists(ref)
std::vector<kg::EntityRef> related(ref, relation)  // committed, then pending
bool        was_written(ref, key)  // did THIS plan touch it?
```

### Why `was_written` exists, and is not redundant

`property()` tells you what a value will be. `was_written()` tells you
whether this plan touched it. **These are different questions**, and the
difference is not academic: writing the value something already held is
invisible to a value read and is still a write.

Aging needs the second question. "Which characteristics has this rule
already spent" cannot be answered by comparing values, because a
reduction of 0, or a reduction that happens to land on the old number,
looks like nothing happened.

Collapsing the two is a real bug with a test that catches it:
`tests/test_planned_world.cpp`, `a_write_of_the_same_value_is_still_a_write`.

### Reads see every earlier op and never their own

`property()` scans the plan newest-first, so an op sees everything
planned before it and never itself. This is the same rule PostgreSQL
implements with a command counter to avoid the Halloween problem. Here
it falls out of the scan direction, and it is written down so it stops
being an accident.

### The invariant that makes it cheap: plans are additive

`apply_kg_ops_atomically` refuses `destroy_entity` and
`play_cinematic`, so **no op in a plan removes anything**. No entity
disappears, no relation is cut, no property is unset.

That is why this view needs no tombstones, no whiteout entries and no
"is absence real or is it hidden" logic, all of which overlay systems
normally need. `tests/test_planned_world.cpp`,
`a_plan_cannot_destroy_so_the_view_needs_no_tombstones`, asserts the
refusal, so the day destroy becomes plannable that test goes red and
sends whoever changed it to this section.

### Lifetime

It holds references and owns nothing. **Valid only while the plan
that produced it is alive. Do not store it.**

Anything that must outlive the plan takes copied values instead. That
is why `AttributeSelectionRequest` carries `current` and
`already_taken` as vectors rather than a view: a choice handed to a
player, or to a language model, is answered long after the plan is
gone.

---

## Why it is built this way

**The view is deliberately not an overlay installed under `KGModule`.**
Physics, rendering and every other consumer must keep reading committed
state and must never learn that a rules batch is in flight. The shape
is the one EF Core calls a `LocalView`: a second read surface beside
the unchanged primary one, chosen explicitly by the code that wants it.

PhysX shipped the other design, promising that reads reflected changes
during simulation, and removed it in PhysX 5 in favour of "do not touch
it during simulation". That warning is why `KGModule` was left alone.

**Rejected, and why:**

- **Datomic-style speculative values** (`d/with`), where pending state
  is an ordinary database value you query normally, is the cleanest
  answer in the literature. It requires the graph to be an immutable
  persistent structure. That is a rewrite of the KG and every consumer,
  not a feature.
- **Applying ops eagerly and undoing on failure** would give
  read-your-own-writes for free, and would break the guarantee in
  Part 1: mid-rule state would become visible, and a chooser can be a
  human or a model taking arbitrary time.
- **MVCC, snapshots, savepoints.** All of it exists so a *concurrent*
  reader can see a consistent past while a writer mutates. The engine
  is single-threaded here with one plan at a time.

---

## Where things live

| Thing | File |
|---|---|
| Bus and channels | `include/logosphere/events/event_bus.h` |
| Channel, signals, journal | `include/logosphere/events/event_channel.h` |
| Journal renderers | `include/logosphere/events/journal_render.h` |
| Transactional emit (detach, buffer, flush) | `src/kg/kg_ops_transaction.cpp` |
| `PlannedWorld` | `include/logosphere/rules/planned_world.h`, `src/rules/planned_world.cpp` |
| Handler context carrying the view | `include/logosphere/rules/outcome_executor.h` |

| Behaviour | Test |
|---|---|
| Bus subscription | `tests/test_damage_events.cpp` |
| Journal ring, cursors, `collect_since` | `tests/test_event_log.cpp` |
| KG property events | `tests/test_kg_setproperty_events.cpp` |
| KG relation events | `tests/test_relation_events.cpp` |
| A failed batch emits nothing | `tests/test_kg_ops_transaction.cpp` |
| Bus silent and graph unchanged mid-rule | `tests/test_outcome_executor.cpp`, `the_bus_is_silent_until_the_whole_rule_commits` |
| `PlannedWorld` resolution, provenance, ordering, additive invariant | `tests/test_planned_world.cpp` |

Deeper consumer guide for the journal, including history renderers and
the ops write-back loop: [KNOWLEDGE_LAYER.md](KNOWLEDGE_LAYER.md).
