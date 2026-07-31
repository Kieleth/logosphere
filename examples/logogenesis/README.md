# Logogenesis

You stand in the construct: a black void with a phosphor-green
grid, Matrix-style. A prompt asks:

> What would you like to create today?

Type it, and it exists. "A beautiful tree." "Another one, a big
Red Wood." "Grass on the floor, with red tones below the redwood."
Each request goes to an LLM whose only vocabulary is the engine's
KG-ops grammar over SEED entities; the materializer grows each seed
through the worldgen generators, then destroys it. The ontology is
the creative vocabulary; the KG is the transmutable medium. This is
the engine's thesis as a demo.

## Run

```bash
cmake --build build --target logogenesis
ANTHROPIC_API_KEY=sk-ant-... ./build/logogenesis/logogenesis
```

Optional: `LOGOGENESIS_MODEL=claude-sonnet-5` (default
`claude-haiku-4-5`). Without a key an offline gardener plants one
oak per request, so the loop stays demoable.

Type in the chat window and press Enter. Close the window to quit
(the chat captures the keyboard while focused, ESC included, a
known ChatWindow limitation, see below).

## How it works

1. **Schema is the spec sheet** (`schema/logogenesis.yaml`):
   `TreeSeed` / `GrassSeed` / `RockSeed` carry validated slots
   (positions, species enum, colors, sizes, every range enforced by
   the OntologyValidator). The system prompt embeds the ontology
   slice of these types verbatim.
2. **Chat → brain**: the ChatWindow's pending-submit queue feeds an
   async responder over `LLMSystemHTTP` (Anthropic; system block via
   the narrative path for prompt caching). The user prompt carries a
   Knowledge-layer query of what already exists, so "another one"
   and "below the redwood" resolve.
3. **Ops**: the reply is one JSON object `{thoughts, ops}`,
   `kg::parse_kg_ops` → `validate_kg_op` → `apply_kg_op`. Thoughts go
   back to the chat.
4. **Materializer**: seed entities are read into generator specs
   (`TreeSpec`/`GrassPatchSpec`/`RockSpec`, species presets + color
   overrides), destroyed, and grown via the deferred KG-native
   generators, which self-queue activation. Budget: 5 seeds per
   message, 60 total.

Tests: `at_logogenesis_creation` drives the whole chain headless
with a scripted responder (no network).

## LLM regression evals

`eval_logogenesis_llm` runs canonical requests against the REAL
model, headless, and asserts RANGES on what lands in the engine,
the ranges are the living spec of the words:

- "a beautiful tree" → light first, one Tree, >=20 particles
- "a lush carpet of grass" → coverage >=150 m2, >=400 blades,
  density >=1.5 blades/m2
- "a small gray rock" → one Rock, <=60 particles (small means small)

Requires `ANTHROPIC_API_KEY` (skips cleanly without, CI-safe). One
model call per case, cents per run, deliberately NOT in the default
battery. This is the prompt-iteration loop: change the prompt or
model, run the evals, no window needed. First live run caught the
carpet-density gap (1.15 blades/m2 vs the 1.5 spec) and drove the
density-arithmetic prompt rule.

Known render issue (separate lane, engine/GPU): tree foliage and
shade-side particles read as black squares under single-point
lighting, no ambient term at this scale. Tracked for the render
session.

## Design notes and follow-ups (flow-back to Logotron / engine)

Maintained per the iteration protocol (consume → name friction →
recurring friction graduates into the engine):

1. **Engine fixes already landed from this build**:
   - `validate_kg_op` now range-checks `create_entity` properties
     (was declaration-only, an LLM could seed a 999 m tree that
     `set_property` would have refused).
   - The deferred worldgen path was dead: generators created
     lowercase entity types (`"tree"`, `"rock"`, `"grass_patch"`)
     that ontology validation rejected, returning INVALID_ENTITY
     silently. Fixed to ontology names; `GrassPatch`/`Grass` added
     to the base schema; activators registered.
2. **Materializer bridge candidate**: the seed→spec→generator
   reconciler here is Logotron's roster lever generalized. A third
   consumer makes it an engine mechanism ("materialize KG entity via
   registered generator").
3. **llm_plan generalization**: Logotron's env planning
   (provider precedence, MLX fallback) was NOT copied, this app
   inlines a minimal Anthropic-only read. Two games now want the
   same env logic: engine candidate.
4. **ChatWindow focus**: self-focuses at init and swallows every key
   (ESC included) while focused. Fine for a pure-chat demo; the
   moment a game wants chat + hotkeys it needs a focus toggle and
   ESC-defocus in the engine UI layer.
5. **Prompt patterns**: spatial language ("below the redwood")
   resolves reliably when the world block carries positions and the
   system prompt states the compass convention. Candidate for
   Logotron's Director prompt.
