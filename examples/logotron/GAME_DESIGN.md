# Logotron, Game Design Document

Living design doc. Captures the intent and mechanics as they exist
today, plus what's coming. Updated with each shipped change, not
written up-front. Inspired by the indie tradition of one-to-three-page
GDDs (Stone Librande, *Extra Credits*, *GDC* indie talks), enough to
align on what the game IS, not enough to become a maintenance burden.

Status legend: ✅ shipped · 🔜 in progress or next · 📋 deferred · ❌ cut

---

## 1 · Pitch

You vs. one AI opponent, Tron-style light cycles. Leave a solid light
wall behind you. Force the other rider into a wall before they force
you into one. Beat the AI and the **Weirden Director** (an LLM) designs
the next opponent with new tactics, so no two duels play the same.
Solo, short rounds, no persistent progression outside the current
session.

## 2 · Fantasy

You are outrunning your own death at 90°. Every turn closes a door.
The arena shrinks as your trail fills it. You are not twitch-
reflexing, you are reading the geometry two or three turns ahead. A
round is over in thirty seconds.

Aesthetic is Tron 1982: black floor, neon grid lines, two cyan-or-
orange trails, a blocky bike silhouette, a sky that could be space or
could be nothing.

---

## 3 · Core loop (one round, ~30 s)

1. Spawn, player and AI at opposite corners, both facing north.
2. Steer, arrow keys change the bike's heading (four cardinal
   directions). Speed is constant, no accel.
3. Leave a wall, the line from your last turn to your current
   position is a solid wall that kills anyone who touches it. When
   you turn, that segment "freezes" and becomes permanent for the
   round.
4. Survive, don't crash into: arena boundary, any frozen trail,
   either cycle's live (un-frozen) run, or your opponent's bike.
5. Round ends when the player crashes. The AI dying does NOT end
   the round, the player keeps riding solo until they crash. This
   is explicit (we considered "first crash = over" and rejected it
   because soloing out the clock feels good).
6. Press **R** to restart, in which case the Weirden cooks a fresh
   opponent.

### Win/lose

- ✅ **AI crashed, player alive →** AI dies, player keeps riding.
  One-time log line "AI crashed, player is now soloing". Round
  terminates when player eventually crashes (then it's either AI_WON
  if the AI was still alive, impossible by now, so this only fires
  with DRAW).
- ✅ **Player crashed, AI alive →** AI_WON, round over, "press R".
- ✅ **Both crashed same tick →** DRAW, round over.

## 4 · Controls ✅

| Input | Effect |
|---|---|
| ← → ↑ ↓ | Turn to cardinal direction (west / east / north / south) |
| Mouse X | Rotate rider's head (and vision cone) left / right |
| Shift + arrow | (reserved for fast-mod in bike_viewer) |
| R | Restart round |
| SPACE | Cycle the test-scene light (bike_viewer only) |
| ESC | Quit |

Turning is discrete, you snap to a cardinal axis, no analog
steering. This is non-negotiable for the genre; analog turning
ruins the two-turn-ahead pacing.

**Mouse-look is "point at the world, not at the window"**. The
cursor's screen position is inverse-projected through the iso
camera onto the bike's horizontal plane; the rider's head is
aimed at that world point. Move the cursor northeast of the bike
and the head turns northeast in world coordinates, not
northeast of some fixed window center. The offset from the bike's
heading is clamped to **±135°**, leaving a **90° forbidden arc
directly behind** the rider, you can glance over either shoulder
but cannot rubberneck through the back of your own helmet. The
head catches up to the cursor at a rate-limited speed (`step_head`),
so twitchy mouse flicks still cost real time. This is the same
structural contract as the AI rider's head (§16); the vision cone
(§11) follows the head, not the bike.

## 5 · Arena & geometry

- ✅ 40 m × 40 m square floor. Neon rim on all four sides, doubles
  as a light source.
- ✅ Faint cyan grid painted on the floor every 5 m.
- ✅ Bike position is continuous (floats), not grid-locked. Trails
  are arbitrary-length axis-aligned line segments between turns.
- ✅ Bike speed: `kCycleSpeed` (constant, game-tunable).
- ✅ Wall thickness: `kWallThickness`. Collision band equals half
  of this around the trail line.

## 6 · Bike (the particle rig)

The bike is a `RigidAssembly` of five ELLIPSOID particles:

| Part | Role | Axis |
|---|---|---|
| ✅ body | long spine, cyan (player) / orange (AI) | long along local +Y |
| ✅ wheel_front | dark tire at the forward end | axle along local X |
| ✅ wheel_rear  | dark tire at the back end | axle along local X |
| ✅ canopy | flattened ellipsoid on top, rider position | biased forward |
| ✅ tail_fin | thin vertical blade at the rear | front-back asymmetry marker |

Engine convention: **local +Y = forward**. Assembly yaw follows the
cycle's current compass direction (see `docs/ARCHITECTURE.md`, orientation conventions).
The whole rig is re-synced every frame from the cycle's KG state;
`sync_rigid_assembly` owns the delete-and-recreate.

### Why five particles and not one box
The game is built on a particle-first engine. A box bike would be a
"decorative" cheat, and the engine invariant is *particles are
bodies*. The rig also sets us up for the **explosion effect** (see §9)
we already have the five pieces the crash scatters.

## 7 · Trails

- ✅ Each straight-line run between turns is a `TrailSegment` entity
  in the KG with `start_x/y`, `end_x/y`, `direction`, and
  `owner_cycle_id`. Created on turn via `freeze_run()`.
- ✅ Trail VISUAL is a single thin box particle spanning the run,
  one particle per segment, regardless of length.
- ✅ Active run (live, un-frozen segment between `run_start` and
  current position) is drawn as a separate particle that's recreated
  each frame.
- ✅ Collision treats sealed TrailSegments and opponents' active
  runs as walls.
- ✅ **Crashed-cycle trails are non-lethal.** The second your
  opponent dies, their entire trail stops being a wall (see §8).

### Trail behavior is configurable, "tail packs" / skills 🔜

Trail walls are not inherent, they're *tuned*. Every tail property
below is a per-cycle setting the game layer writes onto the Cycle
entity at round start; default values are hand-tuned for the base
game, but bonus packs, skills, or Weirden personalities can change
them. This keeps the core loop the same while enabling gameplay
variety without ontology churn.

> **Engine enabler (2026-07):** the interaction model
> (`docs/GAME_LAYER.md` §6) now provides the mechanisms these packs
> need, `collides_with` masks make a trail passable to its owner,
> `fade_out` rules give timed fades with safe deletion (the crashed-AI
> fade already runs on one), and `on_timer`/`trigger_profile` cover
> phase windows. The items below stay game work: they are tuning and
> design, not missing engine features.

- ❌ **Lifetime**, disabled for the OSS launch. The previous
  default (15 s lethal, 2 s fade, then delete) created a silent
  window where a fading trail looked lethal but wasn't, which
  read as a collision bug to playtesters. Replacement contract
  for v1: every visible wall is lethal until `clear_trails`
  wipes it (Director mutation) or the player dies and the round
  fully resets. Re-introducing per-cycle lifetime requires better
  visual telegraphing (clear color-warn, slower fade) and lands
  with the bonus-pack work below.
- 🔜 **Thickness**, perpendicular extent of the wall. Thinner
  trails are easier to skim past; thicker ones block more of the
  arena.
- 🔜 **Color / brightness**, cosmetic; slots into bonus packs.
- 📋 **Passable when faded**, flag: if true, once alpha drops
  below some threshold the trail becomes non-lethal before it's
  fully gone. Layers on top of lifetime.
- 📋 **Density gap**, bonus that makes every Nth segment of a
  cycle's trail passable. Creates "teleport" lanes.
- 📋 **Segment decay rate per-cycle**, personalities can extend
  their own trail or shorten it. Slows/speeds play asymmetrically.

All of these are single float/bool properties on the Cycle entity
plus a check in `check_collision` and the fade sync. The collision
test already reads `owner_cycle_id.state`; adding
`owner_cycle_id.tail_lifetime` is a one-line extension.

Implementation order: lifetime first (fixes "arena chokes with
permanent walls" feel), thickness + color next (cheapest expansion
surface for cosmetics and bonus packs), passable-when-faded once
the Weirden Director wants to hand the player a gimme.

## 8 · Death polish

### Opponent (AI) dies ✅
- Bike particles stay at the crash pose, no teleport, no clear.
- The AI's active run is frozen into a `TrailSegment` on the crash
  frame so the whole trail (including the just-drawn segment) joins
  the fade treatment.
- All AI-owned trail walls fade into the floor over **2 s**: `z`
  sinks from `kTrailZ` to 0, alpha from 1 to 0. When fully faded
  the particles are deleted; the TrailSegment entities remain for
  game logic.
- Trail collision disengages at the moment the AI flips to
  `CRASHED`, player can ride through the fading ruin without
  dying on an invisible wall.

### Player dies (placeholder) 📋
- Bike is currently hidden. Round ends. Press R.
- Proper death effect is the **Tron explosion**, scatter the bike's
  particles outward from the impact point, let physics carry them.
  Not blocking the rest of the game; the current placeholder is
  fine for developing the surrounding mechanics.

## 9 · AI opponent ✅ (basic) / ✅ (Weirden scaffolding) / 🔜 (Weirden wired into main.cpp)

- ✅ **Default AI**, simple lookahead that picks a cardinal direction
  avoiding immediate collision. Good enough to lose to a focused
  player but not remarkable.
- ✅ **Weirden Director scaffolding**, complete in
  `examples/logotron/src/director/`. Async orchestrator with a
  pluggable `Responder` callback: production wires
  `LLMSystemHTTP::submit_request` (OpenAI or Anthropic via env var),
  tests inject canned JSON synchronously, offline mode uses
  `make_random_responder(seed)`. Single-turn JSON schema, four
  v1 mutations (spawn_walls, clear_trails, set_speeds, shrink_arena),
  AI-only respawn helper. 17 headless tests cover the full e2e
  (AI dies, Director fires, mutations apply, AI respawns, player
  trails plus director walls plus arena dim survive). See §19.
- 🔜 **Director wired into the live game loop**, the last gate
  before public play. On `announced_ai_crashed_` flip in
  `update_game`, snapshot `GameState` and call `director_.fire()`;
  each frame poll for the response and apply. Particle cleanup +
  motorcycle assembly recreation wraps `respawn_ai`. Falls back to
  `random_director` when no API key is set.

## 10 · Progression

- ✅ **Within a session**: rounds restart with **R** (full reset)
  on player death. AI death is the Weirden hook, not a round end:
  the Director mutates the world and respawns the AI fresh while
  the player keeps riding.
- ✅ **Arena mutators between rounds**, v1 palette ships with the
  Weirden scaffolding. Director's four mutation types: `spawn_walls`
  (permanent lethal lines), `clear_trails` (mercy reset), `set_speeds`
  (per-cycle speed retune), `shrink_arena` (smaller play area).
  Mutations persist across AI deaths until the player crashes; full
  reset on player death. Schema is open, more mutations are a
  one-file addition. See §19.
- 📋 **Best-of-N rounds / score display**, would plug into UI but
  needs a minimal HUD design first. Not in v0.
- 📋 **Mid-round mutations**, Weirden firing during a live round
  rather than only between AI deaths. Hook exists; held back so the
  basic loop ships first.
- ❌ **Currency, unlocks, story**, intentionally not happening.
  This is a playable engine demo, not a product.

## 11 · Visual style ✅

- Dark floor + neon rim lights (arena boundary is self-illuminating).
- Iso camera at roughly (-30, -45, 30) looking at arena center,
  follows the player with gentle tracking.
- Cyan (0.15, 0.95, 1.00) for player, orange (1.00, 0.45, 0.05) for
  AI. Dark-slate (0.06, 0.14, 0.18) for wheels, reads as tire
  rubber, doesn't fight the cyan body.
- Canopy is bright cyan (0.60, 0.98, 1.00) on the player bike, warm
  gold on the AI's, makes the cockpit read from iso distance.
- Shadows: ray-traced on GPU, follow the geometry including the
  bike's yaw (this took multiple rendering-pipeline fixes to get
  right; see `CHANGELOG.md [Unreleased] > Fixed`).
- ✅ **Vision cone / fog-of-war**, 180° forward cone anchored at
  the player's bike position, rotated by the rider's head yaw
  (mouse-steered). Pixels outside the cone (or beyond 18 m) dim to
  ~18% brightness with a foveal blur in the periphery. Drives home
  the embodied-rider framing and gives the arena a Tron/noir mood
  instead of uniform full-bright. **Trail-aware**: the cone is
  occluded by the same geometry that occludes the AI's perception
  layer (sealed TrailSegments, opponent active runs, but not your
  own active run). Each frame the game raycasts 64 angular bins,
  pushes the per-bin nearest-occluder distance through
  `Engine::set_vision_cone_occlusion`, and the Pass-4 Metal kernel
  darkens any pixel beyond that bin's distance. Engine-generic;
  Logomancers can use the same hook.
- ✅ **Speed dashboard**, bottom-right HUD overlay: chunky 3-digit
  velocity readout, segmented 16-cell neon bar from base→max, and
  a "TURN +X.Xs" recency indicator (the same `time_since_turn` the
  speed ramp keys off, §18). Cyan palette + corner brackets +
  layered glow border for the Tron HUD look. Renders after the
  vision-cone post-process so the cone never darkens the gauge.
  Implementation: `examples/logotron/src/hud/speed_dashboard.{h,cpp}`,
  reuses the engine's `ui::Panel` widget + `RenderSystem` primitive
  draw API. No new shaders. Mouse events explicitly ignored so the
  HUD never steals input from the vision-cone aim.

### Visual inspirations, Tron legacy + Ares

We're not going for AAA polish, but the Tron visual language is
strong and cheap to land. Concrete beats from the three films that
we already borrow or plan to borrow:

- ✅ **Tron (1982)**, flat neon on black, geometric primitives,
  bike rig as a rolling silhouette, rectilinear trails. This is
  the baseline the current build already hits.
- 🔜 **Tron: Legacy (2010)**, slower, heavier vehicles; depth-of-
  field darkening around the rider; thicker, glowier light walls
  with visible volumetric edges; cockpit canopy that actually glows.
  Good targets for next visual pass: wall bloom, canopy rim light,
  trail volumetric falloff.
- 🔜 **Tron: Ares (2025)**, muscular, industrial neon (red/orange
  secondary palette), heavier asphalt feel, rider helmet detail
  with a glowing visor strip. Good targets: AI-bike palette sweeps
  per personality (Weirden Director picks a color+vibe pack along
  with behavior), rider helmet as an illuminated particle rather
  than a flat-shaded ellipsoid.
- 📋 **Derezzing effect** across all three films, the pixel-
  shatter crash sequence is iconic and maps onto our particle-first
  engine: blow the bike's five parts outward as independent
  particles with velocity + fading emission. See §9.

Keep them as aspirations, not mandates. The rule: cheap visual
hooks that come out of the particle model (emission, per-part
scatter, cone falloff) win; bespoke pipeline work loses.

## 12 · Audio

❌ None yet. Keyboard click is the only feedback. Adding sound is
post-MVP, a single "crash" cue and a low synth pad for ambience
would be enough to get 80% of the feel.

## 13 · Scope / MVP status

| Feature | Status |
|---|---|
| One arena, two cycles | ✅ |
| Four-cardinal steering | ✅ |
| Continuous-space trails | ✅ |
| Collision vs walls + frozen trails + opponent active run | ✅ |
| AI crash polish (bike stays, trail fades, collision disengages) | ✅ |
| Player crash polish (explosion) | 📋 |
| Weirden Director scaffolding (mutations + parser + orchestrator + offline fallback) | ✅ |
| Weirden Director wired into live game loop | 🔜 |
| Score / best-of-N | 📋 |
| Audio | 📋 |
| Arena mutators | 📋 |

The current slice is a fully playable one-round duel. Everything
above "player crash polish" is polish, not gameplay gap.

## 14 · Non-goals

- AAA production values.
- Online multiplayer.
- Campaign / narrative.
- Input-remapping UI. Arrow keys are the controls, full stop.

## 15 · Known quirks

- Frame rate sits around 30 FPS on M4 Max. GPU budget is fine
  (~8 ms); the cap is a fixed ~23 ms of CPU/GPU pipeline latency
  from the deferred renderer chain. Not a design issue, an engine
  one. Covered in the commit log and perf audits.
- The active-run visual particle is recreated every frame. Cheap
  (one per cycle) but it shows up in "particles created this
  second" telemetry.
- The delete+recreate pattern in `sync_rigid_assembly` produces a
  per-frame KG churn spike. Considered harmless at current entity
  counts.

## 16 · AI philosophy, driver confinement + director omniscience

Two agents, two contracts. The engine is particle-first, so the AI
design leans into that directly.

### The driver (in-world agent)

The rider is a real particle on the bike. It has its own position
and its own `rotation_z` for its head, the bike's heading and the
rider's gaze are independent state. What the rider perceives each
decision tick is:

- A **forward cone of vision**, anchored to the rider's current
  head direction, not the bike's direction. Default 120° FOV,
  10 m range.
- **LOS-occluded** by lethal trails and walls. The opponent can
  hide behind their own trail, physically correct, gameplay-
  relevant.
- A **per-direction crash distance** (proprioceptive): the rider
  feels imminent doom in each cardinal direction without having
  to look. This is the minimum awareness needed to not be
  suicidal; it doesn't know WHAT is there, only how far.
- **Arena edges always known**, they're self-illuminating; the
  rider has perfect peripheral awareness of the outer walls.

Head rotation is **rate-limited**. Turning to look over the
shoulder costs real time, during the swivel, forward view degrades.
Tactics have to *commit* to a look direction and pay the cost.

Tactical code reads only the perceived snapshot (`PerceivedWorld`).
No direct KG access from tactic files. The perception layer is the
one crossing point, by design. This is the no-cheating contract.

### The player is embodied too

The player's rider is the same structural thing as the AI's, a
particle on the bike with an independent `rotation_z`, rate-limited
head, constrained look arc (±135° offset from the bike direction).
Mouse X drives the target yaw; `step_head` enforces the rate limit;
the vision cone (§11) is mounted on the rider's head, not the
bike. So the player suffers the same "turning to look costs time
and forward visibility" tension that the AI does.

Two practical consequences:
- The player cannot crane fully backwards. The 90° arc directly
  behind the rider is a true blind spot, you can check either
  shoulder but not both at once.
- The human is still getting more information than the AI (the
  whole screen above the cone is visible, just dimmed; UI cues
  help). This is deliberate: making the player as blind as the AI
  isn't fun, making them as omniscient as a typical arcade game
  erases the reason the cone-of-vision model exists. The dimming
  is the compromise.

### The director (meta-agent, outside the world)

The **Weirden** stands outside the game. It has full ontology-KG
access and can mutate it:

- Change the AI's personality / tactic weights between rounds.
- Spawn, remove, or modify entities, pillars, moving walls,
  shrinking floor.
- Introduce new obstacle types, rewrite the arena's shape.

The KG is our *transmutable medium*. This is the Logosphere stance
on LLM-guided adversarial AI: the model doesn't play the game, it
edits the game. The driver reacts; the director reshapes.

In v1 (the OSS launch) the Director picks 1-3 entries from a typed
mutation palette: `spawn_walls`, `clear_trails`, `set_speeds`,
`shrink_arena`. Personality-blend mutations (`AIPersonality`
preset swap, head-swivel tuning) are a follow-up once the live
loop has shipped and we can read player feel. Architecture
details and the JSON shape live in §19.

### Why this split matters beyond Logotron

Any game built on Logosphere that wants intelligent opponents
should use this shape. In-world agents go through perception; a
separate omniscient director is the only place LLM narrative and
game-mutation code lives. Keeps the reasoning clean, the cheating
surface small, and the engine's ontology-graph at the center of
"how the game bends."

## 17 · AI iteration loop, session recording + feedback

Designing AI is not one commit, it's a cycle: run, read back,
annotate, change the design, re-run. We bake that cycle into the
engine so each tuning pass is cheap and traceable.

### What gets captured

Every process launch opens a **telemetry Session** (engine primitive,
`include/logosphere/telemetry/session.h`):

- Directory: `~/.logosphere/sessions/<build_sha>/<launch_utc>_N/`
  (per-commit folder keeps experiments separate when the build
  changes mid-session).
- `session.json`, build SHA + `git describe` + launch UTC + end UTC.
  The build SHA comes from `include/logosphere/build_info.h`, which
  CMake regenerates on every configure from the current git state.
- One `<instrument>/` subdirectory per game-registered Instrument.
  Logotron ships `logotron/` with the **AIInstrument**.

Logotron's AIInstrument writes:

- `round_NNN.jsonl`, one JSONL line per AI decision tick: timestamp,
  self pose, head yaw (current + target), per-direction lethal
  lookahead, opponent visibility + position, chosen direction.
- `round_NNN_meta.json`, round summary: personality, outcome,
  duration, decision count, spawn pose.

A full round at 6 decisions/sec for 30 s is ~180 lines of
self-describing JSON. `cat`, `less`, `jq`, and `grep` are the tools.

### The review + feedback workflow

`scripts/logotron_review.py` is the entry point:

```bash
scripts/logotron_review.py                    # list sessions
scripts/logotron_review.py <session_dir>      # per-round narrative
scripts/logotron_review.py <session_dir> 3    # full tick table for round 3
```

Each round's narrative collapses the JSONL into a one-line summary:
`first_turn=5.7s, sighted_op@3.2s, turns=4`. Enough to spot "the AI
never saw the opponent" or "it turned too late".

**Annotation convention**: a reviewer wanting to leave feedback on a
specific round creates `round_NNN_notes.md` alongside the jsonl. The
file can be anything, prose, a checklist, a pasted conversation.
Git-tracked review dirs can be pulled into the repo later if a
session becomes a canonical reference ("this is the behavior we're
fixing").

### Extending, engine boundary

Games declare their own Instruments by subclassing
`logosphere::telemetry::Instrument`. The base class provides a
thread-safe JSONL append; subclasses decide structure. This is an
engine-level extension point: Eden or any future game gets the same
scaffolding without touching `src/telemetry/`.

The split follows the same rule as the agent boundaries in `docs/ARCHITECTURE.md`:
engine provides the primitive (session + instrument + build ID),
game provides the schema (what fields to record).

## 18 · Speed model + locomotion evolution 🔜

The current build is **constant-speed, cardinal-only, arrow-key
steered**. `kCycleSpeed = 5.0` is a hardcoded `constexpr`, heading
is a `Direction` enum, turns are 90° snaps. That's enough for a
playable prototype but flat, there's no skill expression beyond
pathing. This section captures the plan to evolve Logotron into
something with a real skill ceiling, without losing what works.

### What we're borrowing (and what we're not)

**Not Tron canon.** The films (1982, Legacy, Ares) and all Disney
games have constant-speed cycles. Wall-grinding for speed is a
**fan-game mechanic**, invented by *Armagetron Advanced* (open-
source Tron-clone) and now genre-canonical in that community. We
adopt it deliberately because it's the proven template for a Tron-
clone with real skill expression. The aesthetic stays Tron; the
physics borrow from Armagetron.

The loop: **closer to a wall = faster acceleration; turning costs
speed; skilled play = grind tight lines with minimal turns.** The
punishment for cowards (hugging the open arena = stuck at base
speed) is as important as the reward for grinders.

### The model (Armagetron-faithful, parameter names adopted verbatim)

All of these live as float properties on the `Cycle` entity in the
KG, so the Weirden Director (§16) can mutate them pre-round and
during play, and bonus packs / skills (§7) can permute them.

**Core speed state**
- `base_speed`, cruising speed when coasting in open arena.
- `current_speed`, runtime value. Drifts toward `base_speed` via
  asymmetric decay: slow when above (boosts persist),
  fast when below (recover quickly). Defaults `decay_above = 0.1/s`
  vs `decay_below = 5/s`.
- `speed_min_frac`, `speed_max_frac`, as fractions of `base_speed`.
  `max_frac = 0` means uncapped (Armagetron default).

**Wall-proximity acceleration** (the signature loop)
- `wall_accel`, strength of the accel pulse per tick.
- `wall_near`, max distance where walls accelerate you (default 6 m).
- `wall_accel_offset`, additive distance offset preventing divergence as d → 0.
- Falloff is continuous, `accel ∝ (wall_near − d) / (d + offset)`. Multiple nearby walls sum.
- Per-wall-class multipliers: `accel_mult_self` (your own trail),
  `accel_mult_enemy` (opponent trail), `accel_mult_rim` (arena edge
 , default **0** per Armagetron tradition; edges don't grind).

**Geometry multipliers** (stack on top of wall accel)
- `slingshot_mult`, you're between your own wall and another wall.
  Rewards committing to a trap you set for yourself.
- `tunnel_mult`, you're between two walls, neither yours. Shared
  corridors become speed highways.
- Mutually exclusive; only one applies at a time.

**Turn cost**
- `turn_speed_factor`, multiplier applied to `current_speed` on
  every turn (default 0.95). Every direction change shaves 5%.
  Skilled play minimizes turns.

**Rubber (collision forgiveness)**
- `rubber_max`, error-budget capacity.
- `rubber_refill`, refill rate when clear of walls.
- `rubber_burn_rate`, how fast proximity to walls burns it.
- Inside the rubber band (close but not quite touching), rubber
  depletes instead of crashing. At zero, the next frame inside the
  band is lethal. This is what *enables* skilled grinding,
  without rubber, players can't commit to tight lines.

**Manual throttle / brake** (added in phase E)
- `throttle_kick`, per-frame speed bump while W is held.
- `brake_decel`, per-frame decel while S is held, burns rubber.

### What lives in the KG, and why

Every knob above is a float on the `Cycle` entity, **not a C++
constant, not a CLI flag**. Three consequences:

1. **Per-cycle asymmetry.** Player and AI can have different
   tunings. The director can hand the player a gimme (low rubber
   for AI, high rubber for player) or stack the deck.
2. **Weirden-driven mutation.** The director (§16) reads and
   writes these like any other KG state. Personalities are
   personality + speed profile. Arena-mutation hooks can shrink
   `wall_near` mid-round to tighten everyone's grinding.
3. **Bonus-pack composability.** §7 already says trail behavior
   is configurable; the speed model joins that same mechanism.
   "Grip boots" = `+50% wall_accel` for one round. "Glass cannon" =
   `max_frac = 2.0` but `rubber_max = 1`. Emergent combos fall out.

### Phases, shipping one feature at a time

Each phase is playable on its own; we land one, the user plays it,
we update this section with ✅, and move on. Defaults preserve
current feel until a knob is actually turned on.

**A note on phase A's scope.** The original take-1 was "plumbing
only, move `kCycleSpeed` into the KG, no mechanics change."
Mid-planning we collapsed it into a working mini-mechanic: the
**recency-of-turn ramp**. Same skill loop as Armagetron's wall-
grind ("commit to long lines, turning is costly") but no perception
changes, no collision changes, no geometry assumptions. Just
`current_speed = clamp(base + ramp * time_since_turn, base, max)`,
with `turn()` snapping it back to `base`. Keeps phase A meaningful
to playtest in isolation, you can immediately feel that staying
straight pays off, while the KG fields it lays down are the same
ones B/C/D will write into. Defaults still preserve legacy
constant-speed (max == base, ramp == 0); main.cpp opts in for
the actual game cycles.

| Phase | Scope | State |
|---|---|---|
| A | **Recency-of-turn ramp** (simpler take-1, replaces the "plumbing only" original scope). `base_speed` + `max_speed` + `speed_ramp_rate` + `current_speed` + `time_since_turn` on Cycle. Speed climbs linearly from base to max along straight runs at `ramp_rate` m/s²; every direction change snaps it back to base and restarts the clock. Wall-grind (B) is a richer model on top of the same KG fields. | ✅ |
| B | Passive wall-grind acceleration. `wall_accel` + `wall_near` + offset + asymmetric decay. Perception grows `current_speed` + `nearest_wall_distance`. | 📋 |
| C | Slingshot + tunnel + per-wall-class multipliers. Perception classifies nearby walls by owner. | 📋 |
| D | Rubber system. `check_collision` becomes non-binary inside a rubber band; AI tactics read rubber. | 📋 |
| E | WASD steering. A/D as snap-turn (still on grid), W/S as throttle / brake. Arrow keys stay. Mouse still drives the head + cone (§11). | 📋 |
| F | **Continuous rotation**, replace `Direction` enum with `float heading_rad`. Collision, perception, tactics, trail geometry all ripple (~40–50 code sites). Gets its own plan when A–E have settled. | 📋 |

Order rationale: A unblocks per-cycle variance and teaches the
codebase "speed lives in the KG." B makes the game feel like a
Tron-clone. C + D add the skill ceiling. E makes the controls
expressive enough to exploit B–D. F is the refactor that makes
analog steering feel natural, but we only tackle it after we
know what A–E want to preserve.

### Brainstormed adjacent mechanics (cherry-pick when tuning)

- **Speed-reactive vision cone.** At high speed, `set_vision_cone`
  range shrinks, tunnel-vision effect. Cheap tie-in; one
  expression in the per-frame cone push from §11.
- **Emission-brightness coupling.** Canopy + trail color shifts
  toward white-hot when the bike is grinding. One multiplier on
  the canopy particle's `emission_strength`, nothing more.
- **Drift / skidding.** Holding a turn key + brake preserves
  heading but lets position drift sideways. Tron-cool, pairs
  naturally with continuous rotation in F.
- **Boost pickups.** Rare "boost" particles placed by the Weirden;
  1–2 s multiplicative speed kick on contact.
- **Trail-colored gradient accel.** Already a knob
  (`accel_mult_self` vs `accel_mult_enemy`); just an invitation to
  actually use it during tuning.
- **Slingshot VFX.** Particle spray from the canopy when the
  slingshot condition fires. Echoes Tron: Legacy's volumetric edge
  effects on light walls (§11).
- **Arena-shape-changing Weirden mutations.** Shrink `wall_near`
  globally mid-round (tighter grinding); buff AI's
  `slingshot_mult` to 2.0 and log "Weirden handed the opponent a
  gift." Each one is a single KG write.
- **Replay.** §17 telemetry already captures everything needed for
  deterministic replay. Becomes gameplay gold once speed is
  variable, "the exact frame the grind failed."

### Controls horizon, WASD + mouse

Current: arrow keys for turn, mouse for head/cone (§11, §4).

Target (lands in phase E for the grid, upgraded in phase F):

| Input | Grid-era (phase E) | Continuous-era (phase F) |
|---|---|---|
| Mouse X/Y | Head + vision cone (as today) | Same |
| A / D | Snap-turn ±90° (legacy) | Analog steering while held |
| W | Throttle hold | Throttle hold |
| S | Brake hold (burns rubber) | Brake hold (burns rubber) |
| LEFT / RIGHT | Still work (fallback) | Optional; may retire |

Armagetron itself used keyboard (no mouse-look) because the camera
is a top-down follow, the rider always "looks where they go." We
keep mouse-look because Logotron's embodied-rider §16 treats the
head as real state, and the vision cone needs something to aim.

### Continuous rotation, the big horizon

Phase F is the refactor that turns the grid off. Scope per the
audit: `Cycle.direction` (enum) → `float heading_rad`;
`step_cycle` goes from cardinal switch to `(cos, sin)` math;
`arena.cpp`'s `point_near_run` (axis-aligned check) becomes
point-to-line-segment distance; `PerceivedWorld.lethal_distance[4]`
becomes either a polar grid or a "nearest wall distance + bearing"
pair; every tactic's cardinal switch becomes continuous scoring.

~40–50 code sites, per the Explore audit. Not a side quest. It
will get its own plan when A–E are stable and we know what the
current grid's gameplay feel owes to, so we can preserve that when
the geometry opens up. Explicitly deferred; explicitly scoped.

### Why this section exists

This design doc is the living spec. Each phase ships with a test
file, a CHANGELOG entry, and an update to the table above. When
the user plays a phase and it feels wrong, the fix either updates
the mechanic (rebalance one of the KG defaults) or updates the
plan (a later phase absorbs the lesson). The plan file is
ephemeral; this section is the record.

---

## 19 · Weirden Director architecture ✅ (scaffolding) / 🔜 (live wiring)

The Director is the meta-agent from §16 made concrete. It runs
between AI deaths, mutates the world via a typed JSON schema, and
hands control back to the live loop. This section locks the
contracts; implementation lives in `examples/logotron/src/director/`.

### Trigger

The Director fires on `announced_ai_crashed_` (the moment the AI
flips to CRASHED). The player keeps riding through; mutations
land asynchronously. A fresh AI cycle respawns when the response
arrives. Player death is the round end and triggers a full reset
(the existing `R` path), not a Director call.

### The four contracts

1. **The Responder.** A `std::function<void(prompt, done)>` the
   application installs at startup. Production wraps
   `LLMSystemHTTP::submit_request`; tests pass canned JSON;
   offline mode passes `make_random_responder(seed)`. The
   `Director` class doesn't know which is which.
2. **The JSON schema.** Single-turn:
   ```json
   {
     "thoughts": "Player owns the center, splitting the field.",
     "mutations": [
       {"type": "spawn_walls",  "params": {"walls":[{"start_x":-5,"start_y":0,"end_x":5,"end_y":0,"direction":"EAST"}]}},
       {"type": "set_speeds",   "params": {"ai_max":11.0, "ai_ramp":2.0}},
       {"type": "shrink_arena", "params": {"new_w":32, "new_h":32}},
       {"type": "clear_trails", "params": {}}
     ]
   }
   ```
   Unknown mutation types and malformed entries become warnings
   in `DirectorResponse::warnings`, not fatal errors. Bad JSON
   itself surfaces in `parse_error` and the round restarts vanilla.
3. **The mutation library.** Pure KG functions, no engine, no
   particle system. Each takes `(kg, params)` and returns
   `MutationResult{ok, summary, error}`. v1 ships:
   - `spawn_walls(walls)`: each wall becomes a `TrailSegment`
     with `owner_cycle_id=""` and `spawn_time=0`. Slots into the
     existing collision scan, no engine code touched.
   - `clear_trails()`: wipes every TrailSegment.
   - `set_speeds(player/ai max/ramp)`: writes per-cycle speed
     properties; partial specs only touch named fields.
   - `shrink_arena(new_w, new_h)`: writes new arena dims on the
     Arena entity. Application reads them next round.
4. **AI-only respawn.** `respawn_ai(kg, old_id, spawn_spec)` is
   pure KG: wipes the old AI cycle and AI-owned TrailSegments,
   spawns a fresh AICycle. Player and accumulated director walls
   untouched. The application wraps it with particle cleanup,
   motorcycle assembly recreation, and head-state reseat.

### Mutation persistence

Director walls, arena shrink, and speed retunes persist across AI
deaths until the player crashes. Player death triggers full reset
and the Director returns to its initial state. This is the
escalation curve: each surviving round, the arena gets tighter
and stranger.

### Provider routing

`plan_llm_from_env()` in `main.cpp` already detects the
`OPENAI_API_KEY` / `ANTHROPIC_API_KEY` / `LOGOTRON_LLM_URL`
environment variables and chooses a provider. Native Gemini
support is deferred; a `Custom` provider routes through any
OpenAI-compatible endpoint.

### What v1 ships and what it doesn't

In:
- All four mutation types in the live schema.
- LLM-backed responder for OpenAI and Anthropic.
- Offline `random_director` fallback so the game runs without a key.
- 17 headless tests covering parsing, every mutation, the e2e flow.

Not in v1, deliberately:
- Personality-blend mutations (rewrite the AI's tactic weights).
- Wormhole pairs (the entity type exists, the teleport logic does
  not).
- Mid-round Director firings.
- Native Gemini provider.
- Mutation-aware HUD ("Weirden did X this round" banner).

Each of these is a contained follow-up; none changes the v1
schema or the orchestrator.

---

## 20 · Weirden v1, KG-native creativity + Master Control pause 🔜

§19 describes what shipped in v0.9: a Director that picks 1–3
moves from a closed menu of four mutation types. Playing it shows
the limit. The LLM is rich, the API isn't. The Director has
narrative voice but no real authorial range, because there is no
mutation it could invent that the C++ wouldn't have to add first.

v1 inverts that. The LLM doesn't pick from a menu; it operates on
the world directly through the ontology. The mutation library is
gone. In its place: a typed KG-op vocabulary (`set_property`,
`create_entity`, `destroy_entity`, `set_relation`) that the
ontology schema gates. Adding a new entity type or property to
`schema/logotron.yaml` automatically expands what the Director can
do, no parser change, no application change.

LLM latency stops being a problem to hide. It becomes the moment.
When the AI dies the game pauses, the camera dollies in on the
player's bike, the bike *derezzes* into a glowing disk, a
humanoid Program rezzes alongside, picks up the disk, and holds
it overhead while the disk burns bright, that beat is the
LLM thinking. Response arrives, disk redeploys to bike, Program
disperses, the Director's mutations rez in (walls assembling
line-by-line, arena boundaries pulling closer), camera pulls back,
play resumes. The cinematic IS the loading screen; the loading
screen IS the show.

### Pillar 1 · The KG is the API

What v0.9 calls a "mutation" is, underneath, a small set of KG
writes wearing a typed wrapper. v1 strips the wrapper. The
Director's response payload becomes:

```json
{
  "thoughts": "Player owns the diagonal, funnel them.",
  "ops": [
    {"op":"create_entity","type":"TrailSegment",
     "properties":{"start_x":-5,"start_y":0,"end_x":5,"end_y":0,
                   "owner_cycle_id":"","spawn_time":0,"director_origin":"1"}},
    {"op":"set_property","entity":"@ai_cycle",
     "property":"max_speed","value":11.0},
    {"op":"set_property","entity":"@arena",
     "property":"arena_w","value":32},
    {"op":"create_entity","type":"Wormhole",
     "properties":{"x":-10,"y":-10,"pair_id":"north_west"}},
    {"op":"create_entity","type":"Wormhole",
     "properties":{"x":10,"y":10,"pair_id":"north_west"}}
  ]
}
```

`@ai_cycle` and `@arena` are *symbolic refs* the prompt resolves
on the game side (the prompt tells the LLM these aliases exist for
the singletons it cares about). Numeric ids are also accepted.

The four v0.9 mutation types become idiomatic patterns the prompt
shows as examples, not enum cases the parser enforces. Anything the
ontology declares becomes legal; anything it doesn't is rejected by
the safety layer (next pillar). When we want the Director to be
able to spawn a `MasterControlProgram` entity, we add it to the
ontology and the next prompt build picks it up, no director_parser
change.

### Pillar 2 · The safety envelope is the ontology

Three rules, enforced by a single validator the parser hands every
op to before it touches the KG:

1. **Type must exist.** `create_entity.type` must name an entity
   class declared in `schema/logotron.yaml` (or one of its
   ancestors via the engine ontology, `TrailSegment`, `Wormhole`,
   `Cycle`, etc.).
2. **Property must exist on the type, with the right kind.** A
   `set_property` against `Cycle.max_speed` is fine because the
   ontology says it's a float; a `set_property` against
   `Cycle.color_of_helmet` is rejected because no such property
   exists on `Cycle`.
3. **Range / cardinality, when declared.** The ontology schema
   gains optional `min` / `max` / `step` annotations. The
   validator enforces them. Speed clamp `0 < max_speed < 25`
   stops being a hardcoded check in `set_speeds` and becomes a
   schema annotation on `Cycle.max_speed`.

Validator failures don't crash the round. They become entries in
`DirectorResponse.warnings` (already a thing) and the bad op is
dropped; the rest of the batch applies. The Master Control HUD
shows them so the LLM's mistakes are visible and authorable
("director tried to spawn TitaniumGorilla, type unknown").

### Pillar 3 · The Master Control pause cinematic

This is what makes the LLM the show instead of a wait.

**Trigger.** Same as v0.9: AI crashes (`announced_ai_crashed_`).
The respawn path detours through the cinematic.

**Phase 1, Hold (≤ 200 ms).** `TimeSystem::pause()` (already
exists). Physics + AI sleep; the renderer keeps drawing every
frame so the freeze isn't a stutter. Camera notes the player's
current pose for the dolly-in.

**Phase 2, Camera dolly (≈ 700 ms).** New
`CameraDirector::focus_on(entity_id, distance, tilt, ease_seconds)`
wrapper around CameraSystem. Smoothstep position + look-at over
the ease window. Respects whatever projection mode is active.

**Phase 3, Player bike derez (≈ 600 ms).** New particle effect:
the bike's parts converge to the bike center over the ease window
and reform as a glowing disk (a flat thin SPHERE or short
CYLINDER). The bike entity stays in the KG and the player keeps
their identity; only the visual particles morph. The disk's
`is_light_source` goes true with `emission_strength` ramping
0 → 4. The Tron derez sound (or its absence) is fine here, sound
is post-MVP per §12.

**Phase 4, Program lift (≈ 400 ms).** Spawn a humanoid using the
existing humanoid rig (the engine already has biped locomotion + a
yaw cascade, the agent-boundaries section of docs/ARCHITECTURE.md). It rezzes in with a brief vertical
dissolve, walks one step toward the disk, lifts it overhead.
Idle clip plays through Phase 5.

**Phase 5, Hold while LLM thinks (variable, 0–10 s typical).**
Disk emission pulses subtly. Humanoid idles. Camera holds. The
Director Ledger HUD streams the LLM's `thoughts` string as soon as
the response arrives, you read it before the cinematic resolves.

**Phase 6, Mutation rez-in (≈ 900 ms).** Each KG op the Director
proposed gets a *visual play* if its type defines one:
- `TrailSegment` (director-spawned): the wall draws itself
  end-to-end with a moving glow head, like a cycle laying trail
  but at 5× speed.
- Arena resize: the perimeter walls pull inward visibly.
- Speed change: a brief overlay on the affected bike (a thin
  ring expanding outward).
- `Wormhole`: the pair fades in with a shimmer.
- Anything new: defaults to "rez fade-in over 200 ms".

The visual play is OPTIONAL. Ops without one apply silently. The
Director can mix loud ops (walls, wormholes) with quiet ones
(speed retunes) and only the loud ones land as theater.

**Phase 7, Bike redeploy + program disperse (≈ 600 ms).** Disk
returns to the player's hands position, expands back into the bike
particles in reverse of Phase 3. Humanoid bows / dissolves. Camera
pulls back to the gameplay framing. `TimeSystem::resume()`. Round
continues.

**Total cinematic budget.** 2.4–3.4 s of mandatory beats around the
LLM call. If the response arrives in <1 s the Phase 5 hold
collapses to a minimum of 300 ms so the cinematic still reads. If
the response takes 8 s the player is just watching the show; no
"loading" copy needed.

**Player death.** Player's own death also runs Phases 1–4 and 7,
without the LLM call. It's already the round-end reset, so we just
hand the existing reset path through the same camera+derez sugar.
The death feels like ceremony instead of a fade-to-black.

### Pillar 4 · What the LLM sees

The v0.9 `GameState` snapshot is too narrow, the LLM had no
positions, no causation, no history. v1 sends:

- **Ontology schema slice** (one-shot at startup, then deltas):
  every entity type and property the LLM is allowed to touch,
  with min/max/step annotations.
- **KG snapshot** (per call): every entity with its current
  properties, serialised compactly. Player + AI cycles get pose
  (x, y, direction, current_speed). TrailSegments listed with
  endpoints. Director-origin walls flagged.
- **Causation hint**: short string describing what just happened
  and why ("AI rammed director wall at (3.5, 12.0) head-on at
  9 m/s"). Generated by the existing collision code, threaded into
  `GameState.narrative_hint` (which exists in v0.9 but is never
  populated, see the visibility notes).
- **History**: prior `thoughts` strings + ops summary for the last
  N rounds. Lets the Director remember what it tried.
- **Symbolic refs**: `@player_cycle`, `@ai_cycle`, `@arena` so the
  LLM doesn't need numeric ids.

This bumps prompt size meaningfully but stays well under any
reasonable model's context. We sketch a token budget when we wire
the prompt builder.

### What v1 keeps from v0.9

- The async Responder contract (production wraps `LLMSystemHTTP`,
  tests inject canned JSON, offline mode uses `random_director`).
- The Director Ledger HUD (gains the cinematic phase indicator and
  the validator warnings).
- Mutation persistence rules (director walls + arena dims persist
  across AI deaths, full reset on player death).
- Anthropic > OpenAI > Custom > offline routing.

### What v1 retires

- The four-mutation enum and its parser branches.
- The hardcoded `respawn_ai_in_world` spawn point, Phase 7 spawns
  the new AI cycle wherever the Director's ops put the spawn (or
  defaults if no spawn op fires).
- The "mutations apply synchronously next frame" rule, they apply
  during Phase 6, ordered, with their cinematic plays.

### Implementation phases

Highest level:

- **A · Cinematic shell.** TimeSystem.pause + camera dolly +
  derez/redeploy effect. Mutations still go through v0.9 menu.
  Zero LLM-side change. Lands the show first.
- **B · KG-snapshot prompt.** Prompt builder serialises the world
  + ontology slice. Director still responds in v0.9 menu format.
  Verifies the LLM can author with the bigger context.
- **C · KG-op vocabulary + safety validator.** Parser accepts the
  new `ops` array; validator gates each op against the ontology.
  Menu format kept as a back-compat fallback for one release.
- **D · Mutation rez-in plays.** Per-type visual playback in
  Phase 6.
- **E · Director-authored cinematics.** Director can request
  named cinematics ("spawn Program, do disk-throw at player"),
  the same rez/derez primitives used by the engine, exposed as
  ops the LLM can issue itself.

---

## Change log

Tracked in the repo's
top-level `CHANGELOG.md` (user-facing engine changes). This doc
summarizes design intent. The log docs say *what happened*.
