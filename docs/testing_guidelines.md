# Testing guidelines

Rules earned the hard way. Every one of these has a real failure
behind it, in this repository, usually a test that passed while the
thing it was named after was broken. The examples are kept because a
rule without its scar is easy to argue away.

Read this with `CLAUDE.md`. That file says *what* the engine
guarantees; this one says how a test earns the right to claim it.

---

## 1. Assert the thing the test is named after

The most common failure here is not a wrong assertion. It is a
**missing** one, in a test whose name promises it.

`test_humanoid_strata_walk` was gating, green, and sitting in a list
of locomotion guards. It asserts three things: no gluon-connected pair
separating past 0.5 m, hips not dropping below 0.3 m, bounding box not
exceeding 3.0 m. It never checks that she goes anywhere. Measured, she
stays inside a 6.6 x 7.4 m box for a 190 s run while its phases are
named for a staircase climb and a 100 m sprint. It passed regardless,
and was cited as proof that locomotion worked on raised terrain.

It is a dismemberment guard wearing a locomotion guard's name. It is
now `test_humanoid_strata_integrity`.

**Rule.** If the test is called `..._walk`, distance travelled is an
assertion. If it is called `..._climb`, the height change is an
assertion. Name the file for what it checks, or check what the name
says.

## 2. A test that cannot fail is not a test

Eight shambler AI "tests" in logomancers have zero assertions between
them and an infinite loop each. They exit 0 whether the creature
hunted perfectly or stood still for ten minutes. `test_shambler_ai_01`
is titled "Shambler AI Test"; its mode labelled "GOAP ROAMING" never
calls `init_senses`, so perception returns on its first line and the
random fallback FSM runs. The mode named for GOAP exercises zero GOAP.

**Rule.** Before believing a green run, ask what would make it red. If
the answer is "a crash", it is a smoke test, and it should say so in
its name.

## 3. Prove the check is not vacuous: run the control

A check that always gives the same answer is worse than no check,
because it looks like coverage.

`can_see_point` is a **shadow ray**. Asking "can I see the prey's
centre" is always false, because the prey's own surface is between the
viewer and its centre. An occlusion assertion built on it passed with
a boulder in the way and would have passed with an empty field. It
proved nothing at all.

The fix is not a better threshold. It is a **control**: run the
identical query where the answer must be the opposite.

```
can_see_point to the prey's spot: with a boulder 0, in an empty field 1
```

**Rule.** For every check of the form "X is blocked / absent / zero",
add the case where X must be present. If both come out the same, the
check is measuring nothing.

## 4. Measure first, assert second

Do not write the expected number from your mental model and then bend
the test until it agrees. Run it, print what actually happens, and
then decide whether that is correct.

The predator senses test failed everywhere on its first run. The
instinct was to loosen it. The truth was in the implementation:
`cast_vision_cone` **samples** its cone, 32 rays over 100 degrees is
3.2 degrees apart, which at 10 m leaves 0.56 m between neighbouring
rays, so a 0.5 m animal falls between them. Five of the six pitch
angles aim downward, tuned for "small ground objects". Prey at eye
height gets one ray of six.

That is a property of the sensor, not a bug, and the test was wrong to
expect otherwise. Prey is 2 m across now, and the reason is written in
the file.

**Rule.** Print measured values in every test, always. `[measure]`
lines are not debug noise; they are how the next person learns the
system's real envelope.

## 5. Verify the mechanism, not your own reconstruction of it

The worst failure of this session. I claimed twice that the UI captions
were on screen. My evidence was a screenshot that **my own script had
composited**, reading the scene buffer and the UI overlay buffer and
merging them in Python. Of course the text was in my PNG. Nothing in
that tested whether the *window* composited them, and it did not: the
overlay handed to the display measured 0 pixels.

The honest check reads what the engine actually hands over,
`Engine::get_overlay_staging()`:

```
overlay staged for the display: 201492 pixels, 1434 of them caption text
```

**Rule.** Verify through the same path production uses. If your
verification does work the engine would have to do, you are testing
your verification.

## 6. Negative, edge and absent cases are the test

The happy path is the cheapest and least informative case. Everything
interesting is at the boundary.

Worth having, and each of these found or prevented something:

- a goal sealed behind walls (no path, not a hang)
- a start **inside** geometry, because creatures get nudged into rocks
- coordinates off the edge of the grid
- walking a path past its end, popping an empty plan
- an odour beyond its radius: **never** detected, not merely seldom
- an odour the creature does not hunt
- an already-satisfied goal (valid plan, zero actions)
- an unreachable goal (invalid plan, no actions)
- a cycle that must terminate rather than hang

`max_depth` in the GOAP planner is off by one (issue #34) and only a
boundary case found it: a 2-action plan is refused at `max_depth=2`.

**Rule.** For every capability, test its absence, its limit, and one
step past its limit.

## 7. Instrument before concluding

Guessing is slower than looking, every time.

The predator "moved but not in the logs". The log was right: it was
frozen, and the motion was the camera panning. One heartbeat line per
second, saying where it went and whether that helped, exposed three
separate bugs in a single run: waypoint oscillation across a 0.6 m
threshold, no wander behaviour at all, and freezing when a goal landed
in a blocked cell.

`ParticleTracer` exists for exactly this and is documented in
`CLAUDE.md`. It found #29 and #30 in minutes after hours of theorising.

**Rule.** When something looks wrong, add the probe that shows the
causal chain. Do not reason about what "must" be happening.

## 8. Never loosen an assertion to make it pass

When a test fails, exactly one of these is true: the code is wrong, or
the test's expectation is wrong. Widening a tolerance decides that
question without investigating it.

Legitimate: the occlusion invariant "the cone never reports the prey"
was too strict, because a 2 m animal can legitimately show an edge
while its centre is blocked. Correct fix: sample across the body and
require **full** occlusion.

Illegitimate, and it was caught: raising a performance threshold until
a flaky ratchet went quiet. It was flaky because it averaged spiky
frame times. Medians, then an honest smoke gate, with the reason
written down.

**Rule.** State which of the two was wrong, in the commit message.

## 9. Headless is the source of truth; visual must not diverge

Headless runs in CI and is what everyone else sees. If it passes
headless and fails windowed, that difference is a bug, not a quirk.

Both watchable tests here run the same geometry and the same configs
in both modes, and the window only opens after the assertions pass.
When `World` hardcoded `create_display = false`, visual mode silently
had no window at all and every claim about it was worthless.

UI text rasterises with `create_display = false` (see
`test_ui_overlay_plane`), so **on-screen output is testable headless**.
There is no excuse for an unguarded HUD.

**Rule.** Anything a human is meant to see gets an assertion on lit
pixels, not a promise.

## 10. Watchable tests are a claim about pixels

"It's watchable" is not a code review outcome. Traps paid for here,
all of them repeatedly:

- The window is created **unfocused and behind the launching
  terminal**. Every SPACE press goes to the shell. Raise and focus it,
  and print whether that worked.
- Immediate-mode `draw_text` after `Engine::render()` is the wrong
  slot: `render()` clears the UI dirty bounds and `present()` skips
  frames, so the text is wiped before it is composited. Use a retained
  widget (`TextWindow`); `UISystem::render()` draws those inside the
  render pass.
- An Engine per view means a window per view, which is a flicker and
  nothing else. One window, move the camera.
- The projection is orthographic: moving the camera closer does not
  make anything bigger. `set_pixels_per_unit` does.
- Markers below a few tens of centimetres render as nothing at normal
  zoom. The data can be perfectly correct while the picture is empty.

**Rule.** Capture a frame and *look at it* before telling anyone it
works. Then assert the pixel count so it stays working.

## 11. Known-red is a convention, not an excuse

Real defects found by a good test should not be hidden, and should not
turn CI permanently red either. The convention is XFAIL, from
`physics_guard_runner`: report on every run, never gate, and announce
when it starts passing so it gets promoted.

```
XFAIL: step up: no teleporting (biggest single frame 0.350000 m)  (known-red, not gating)
```

**Rule.** A known-red needs a filed issue and a one-line reason in the
test. Never mark a test XFAIL to silence it, and never promote one
because the runner nagged you: check *what* it asserts first.

## 12. Every delivered feature is visually verifiable

Project rule, stated 2026-08-08 after a feature shipped headless-only:
a feature without a watchable mode is not done, and the visual mode
ships WITH the feature, not as a follow-up.

The convention: `LOGOSPHERE_VISUAL=1 ./build/<name>` opens the window
(raised and focused, ESC quits), lights at strength-in-the-millions,
a `TextWindow` panel for live state, and — because a watchable test is
still a test — the pixels asserted: glyph pixels distinguished from the
panel's background rectangle, frame brightness measured, headless and
windowed printing the same `[measure]` lines. `at_predator_hunger` /
`at_predator_hunger_visual` is the reference pair: the first proves the
numbers in the headless core, the second shows the carcass shrinking
bite by bite.

**Rule.** Before calling a feature delivered, answer: what does the user
RUN to watch it work? If the answer is "read the measure lines", it is
not delivered.

## 13. Agent and tool reports are hypotheses

Subagent findings, search results and summaries are leads. Several in
this session were subtly wrong in ways that would have produced
confident, incorrect work.

**Rule.** Grep-verify a claim before building on it. Quote file:line
in the commit so the next reader can check you.

---

## The shape of a good test here

```cpp
// Why this exists, and what broke to make it necessary.
//
// What a green run entitles a reader to believe, and what it does not.

  [measure] ...                 // real numbers, every run
  CHECK(measured_thing, "...")  // the claim in the name
  CHECK(control_case, "...")    // proof the check can fail
```

- one subject, isolated, so nothing else can be blamed
- no absolute world heights: ask the ground locator
- headless-safe if it touches no physics or rendering, so Linux CI
  covers it (`add_headless_test`)
- failure messages carry the measured value, not just "expected true"
