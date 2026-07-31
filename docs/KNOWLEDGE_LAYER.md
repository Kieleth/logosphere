# The Knowledge Layer — Consumer Guide

How a game (or any tool built on Logosphere) turns world state and
world history into *meaning*: filtered views, compact histories,
prompt-ready blocks, and a validated write-back path. This is the
tutorial-style guide; the terse API reference lives in
[`GAME_LAYER.md`](GAME_LAYER.md) sections 2–3.

The layer in one sentence: **the engine owns the derivation machinery
(select, project, aggregate, history, address); your game owns the
meanings** (which facets exist, which queries run, what the English
says, what the agent decides).

Who this serves — all four consumer classes, with the same five
operations:

| Consumer | Example |
|---|---|
| Meta-agents | An LLM director that reshapes the arena |
| In-world agents | A creature AI scanning for threats |
| Humans | A debug inspector, an analytics panel |
| Machines | Replay, save/load, tests |

A prompt, a perception snapshot, a save file, and an inspector panel
differ only in filter arguments and output encoding. Everything below
is one pipeline:

```
   KG state ──┐
              ├── facets → queries → renderers ──→ your consumer
   journal ───┘                                        │
      ▲                                                │ decides
      └──────────── ops (validated writes) ◄───────────┘
```

---

## 1. Declare meaning: facets

Your ontology types carry free-form semantic tags. Declared in your
game's schema YAML:

```yaml
  Crop:
    is_a: WorldEntity
    annotations:
      facets: world, harvestable    # comma-separated
  ShopMenu:
    is_a: WorldEntity
    annotations:
      facets: ui
```

Then regenerate (`python scripts/generate_ontology.py`) and commit
the YAML plus the regenerated files, as with any ontology change.

Rules that keep this honest:

- **The engine defines no facet names.** `world`, `ui`, `debug`,
  `harvestable` are YOUR conventions, exactly like your entity types.
- **Explicit per concrete type, no `is_a` inheritance.** A facet you
  didn't write isn't there. (Inheritance would make facet *removal*
  inexpressible.)
- Runtime lookup: `registry.hasFacet("Crop", "world")`,
  `registry.typesWithFacet("world")` (sorted). Tests and tools can
  tag at runtime with `reg.addFacets("Crop", {"world"})`.

Why bother: selection by facet makes leaks *structurally impossible*.
Logotron's splash screen once spawned its logo as fake trail entities
and every LLM prompt carried ~150 phantom walls; with facet
selection, a `ui` type can never enter a `world` view, no matter who
forgets what.

## 2. Read state: queries

One value type serves every consumer (`logosphere/kg/kg_query.h`):

```cpp
#include "logosphere/kg/kg_query.h"

kg::Query q;
q.facets = {"world"};                  // or/and explicit q.types
q.where  = {{"state", "ripe"}};        // ANDed string-equality
q.props  = {"kind", "growth_stage"};   // projection; empty = all
q.limit  = 64;                         // row budget

auto rows = kg::run_query(kg, q);      // structs, for game code
size_t n  = kg::count_query(kg, q);    // aggregate without materializing
auto json = kg::render_query_json(rows);  // JSON lines, for an LLM
```

Each `QueryRow` is `{id, type, props}` with props sorted by key.
Guarantees you can build on:

- **Deterministic**: explicit types iterate in the order you gave
  them, facet-expanded types append sorted, entities sort by id,
  properties by key. Rendered output is diffable and cacheable.
- **Unknown types/facets select nothing** (they don't throw).
- `render_query_json` emits one object per line:
  `{"id":7,"type":"Crop","props":{"kind":"corn"}}` — the same stable
  format the legacy `serialize_kg_snapshot` produced (that function
  is now a deprecated wrapper over this).

Composition beats features. Two real patterns from Logotron's
Director (its trail field would otherwise dominate every prompt):

```cpp
// Different projection per type: run two queries, concatenate rows.
kg::Query actors;  actors.facets = {"world"};        // full detail…
// …but pull trails out and re-query them projected + budgeted:
kg::Query trails;
trails.types = {"TrailSegment"};
trails.props = {"start_x","start_y","end_x","end_y","owner_cycle_id"};
auto rows  = kg::run_query(kg, actors);
auto tr    = kg::run_query(kg, trails);
if (tr.size() > 60)                       // newest-N: trim game-side
    tr.erase(tr.begin(), tr.end() - 60);
rows.insert(rows.end(), tr.begin(), tr.end());
```

(If your game needs per-type projection or ordering often enough to
hurt, say so — that is exactly the signal that promotes it into the
engine. See the iteration protocol at the bottom.)

## 3. Read history: the journal

Every `EventChannel` (see `GAME_LAYER.md` §3 for the channel list)
journals its events: stamped `{seq, frame, game_time}`, retained by
capacity (default 1024/channel), never by frame count. Two access
modes:

```cpp
// Subscription: one reader per consumer, held for its lifetime.
// A drain returns everything since YOUR last drain — that is the
// cursor, and it makes "since I last looked" literally true.
auto reader = bus.deaths().create_reader();          // at init
...
for (const auto& ev : reader.drain()) { /* each fire */ }
reader.dropped_count();   // events lost to capacity eviction — loud

// Query: stateless history access, no reader needed.
auto entries = bus.deaths().collect_since(seq, /*max_n=*/32);
// entries[i] = {event, seq, frame, game_time}
```

Rules:

- `create_reader()` starts at the head — events from before the
  reader existed are `collect_since` territory, not re-delivered.
- A reader that falls behind a full ring skips forward and counts
  the loss. Never silent, never stuck.
- **Emit your own game events.** The deaths channel means *deaths*:
  the engine emits them where it knows (DamageSystem on HP zero) and
  your game emits them where IT knows. Logotron's derez logic:

```cpp
logosphere::ontology::DeathEvent ev;
ev.target_entity_id = std::to_string(dead_entity);
ev.source_entity_id = std::to_string(killer_entity);   // attribution
ev.payload_keys   = {"cause", "x", "y"};
ev.payload_values = {cause, x_str, y_str};
engine.get_event_bus().deaths().emit(std::move(ev));
```

- `kg.setProperty` already journals every real property change into
  `state_changes()` with `{property, value, prev}` payload — your
  mutations are history without any extra code.

## 4. Render history: the block renderers

`logosphere/events/journal_render.h`, header-only, both take the
stamped entries from `drain_entries()` / `collect_since()`:

```cpp
// One line per entry, any channel:
//   [412] t=73.1 DEATH target=7 source=3 cause=wall
logosphere::render_journal_compact(entries);

// state_changes folded into NET deltas — per (entity, property),
// first prev -> last value, no-op round-trips dropped:
//   5 arena_w: 50 -> 44
//   7 max_speed: 8 -> 12
logosphere::render_state_deltas(entries);
```

The delta renderer is the state-side twin of a death log: an LLM
reasons far better on "what moved since your last intervention" than
on re-diffing two full snapshots. Filter the entries first (by
entity set, by property denylist) — which entities matter and which
properties are churn is *your* policy. Logotron keeps kinematics
(`x`, `y`, `current_speed`) out and folds only actors + arena.

## 5. Write back: the ops loop

The validated mutation path any meta-agent uses
(`logosphere/kg/kg_ops*.h`):

```cpp
auto parsed = kg::parse_kg_ops(llm_json);   // {"ops":[...]} or [...]
for (auto& op : parsed.ops) {
    // resolve @aliases game-side first (EntityRef.symbolic), then:
    auto v = kg::validate_kg_op(op, kg, kg.getRegistry());
    if (!v.ok) { log(v.reason); continue; }     // type exists? prop
                                                // declared? range ok?
    auto r = kg::apply_kg_op(op, kg);           // mutates the KG
}
```

Five ops: `create_entity`, `destroy_entity`, `set_property`,
`set_relation`, `play_cinematic`. Targets accept numeric ids or
`@symbolic` aliases — the alias vocabulary is yours (Logotron:
`@player_cycle`, `@program_1`, `@arena`); the engine only carries the
`EntityRef`. Numeric range annotations from your schema
(`minimum_value`/`maximum_value`) are enforced by the validator, so
an LLM cannot shrink your arena to 0×0 no matter what it emits.

## 6. Worked example: a 40-line rule-based director

No LLM required — the same views drive any judgment. An orchard
game where a director keeps the world in tension:

```cpp
void OrchardDirector::tick(kg::KGModule& kg, logosphere::EventBus& bus) {
    // KNOWLEDGE: how ripe is the world?  (query + count)
    kg::Query ripe;
    ripe.facets = {"harvestable"};
    ripe.where  = {{"state", "ripe"}};
    size_t ripe_n = kg::count_query(kg, ripe);

    // INFORMATION: what happened since my last tick?  (journal)
    auto deaths = death_reader_->drain();          // held since init
    auto delta  = logosphere::render_state_deltas(
        state_reader_->drain_entries());           // net changes

    // WISDOM (yours): too ripe and too quiet -> send crows.
    if (ripe_n > 10 && deaths.empty()) {
        kg::KGOpCreateEntity op;
        op.type = "CrowFlock";
        op.properties = {{"size", std::to_string(ripe_n / 5)}};
        auto v = kg::validate_kg_op(op, kg, kg.getRegistry());
        if (v.ok) kg::apply_kg_op(op, kg);
    }
}
```

Swap the `if` for an LLM call whose prompt is
`render_query_json(rows)` + the delta block + your persona, and you
have Logotron's Weirden. The views are identical; only the judgment
changed.

## 7. The full reference consumer

Logotron's Director exercises every mechanism on this page in one
place — read it as the living tutorial:

| Mechanism | Where |
|---|---|
| Facet tags | `examples/logotron/schema/logotron.yaml` (`facets: world`) |
| Facet query + trail budget/projection | `examples/logotron/src/logotron_app.h`, `fire_director` |
| Game-emitted deaths with attribution | same file, `emit_death_event` |
| Death cursor → prompt block | same file (drain at fire) + `director/director.cpp` |
| Delta block + churn filtering | same file (state_reader_) |
| Ops loop with @aliases + cinematics | same file (apply section) + `director/` |

## 8. Locked by tests

| Behavior | Test |
|---|---|
| Query selection/projection/limit/count, facets, JSON | `tests/test_kg_query.cpp` |
| Journal ring, cursors, loss accounting, collect_since | `tests/test_event_log.cpp` |
| Renderers: compact, delta folding, churn semantics | `tests/test_journal_render.cpp` |
| End-to-end: mutation → journal → prompt block | `examples/logotron/tests/at/at_full_game_loop.cpp` |
| Ops parse/validate/apply | `tests/test_kg_ops_parse.cpp`, `test_kg_ops_apply.cpp`, `test_ontology_validator.cpp` |

## 9. When the layer doesn't fit: the iteration protocol

If you hand-roll the same thing twice, or can't express a filter
cleanly, that is engine feedback, not your workaround to keep. The
protocol (by design): games consume, friction gets named
in the PR, recurring friction graduates into the engine. Current
named candidates, waiting on a second consumer: per-type projection,
query ordering (newest-N), journal entry filtering, registered
views, derived facts.
