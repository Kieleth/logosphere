---
name: logosphere-tests
description: Use when writing, converting or reviewing ANY logosphere test. Every test must run both headless (CI, asserting, capturable) and windowed (owner QA), from one source with no duplicated scene code. Carries the seven acceptance criteria and the working patterns for light, camera, ESC, SPACE, FPS and on-screen readout.
---

# Writing a logosphere test

**Status: CONTRACT COMPLETE, PATTERNS PENDING.** The seven criteria below
are the owner's, verbatim, and are binding now. The implementation
patterns are being mined from the tests that already work; until that
lands, satisfy the criteria by copying `tests/test_rube_goldberg_machine.cpp`
and `tests/test_knockback_scene.cpp`, which are the two closest to
correct.

## Why this exists

The owner does the final QA on every physics change, by watching it.
A number he cannot see is not evidence he can act on. On 2026-08-16 he
was handed a window that was **black** (the scene's lights stayed put
while the body fell away from them) and that he **could not close**
(ESC was never polled). Both defects were in a test written the same
hour by someone who had read the visual-test documentation.

That is the failure mode this skill exists to make impossible: a test
that is technically correct headlessly and useless to the person who has
to sign it off.

## The seven criteria (owner, 2026-08-16, verbatim)

> a) every test needs to be headless and head
> b) the headless needs to be programatically apt to capture and they are
>    reflections one of the other and they share code for the tested
>    parts, no duplication
> c) interactie means there's proper light, zoom, camera position and
>    angle to see the test
> d) ESC stops always the test
> e) we use SPACE to move towards the test
> f) always FPS need to be present in the test
> g) clear assertions in each of the tests and proper logging is
>    presented in the ui for the test Always

## What each one means, stated so it can be checked

- **(a) Both modes, always.** Headless is the default and is what CI
  runs. `INTERACTIVE=1` opens the window. Neither is optional, and a
  test that ships headless-only is not finished.
- **(b) One scene, two drivers.** The bodies, the forces and the
  measurements are written ONCE and both modes run the same code. If the
  windowed path re-creates the scene, the two will drift and the thing
  the owner watched will not be the thing CI asserted. Headless must
  also be able to CAPTURE: deterministic frame dumps, so a visual claim
  can be checked by a machine and not only by an eye.
- **(c) Visible means visible.** Light close enough to matter (inverse
  square is not a suggestion: one distant bright lamp leaves a scene
  black), zoom framing the subject, camera position and angle chosen so
  the thing under test is what you are looking at. A falling body needs
  the lights to fall with it or it goes dark in under a second.
- **(d) ESC always quits.** Every frame, unconditionally, plus the window
  close button. A window the owner cannot close is worse than no window.
- **(e) SPACE moves toward the subject.** The owner drives in to look
  closer.
- **(f) FPS on screen, always.** Every test, no exceptions.
- **(g) Assertions are clear, and the log is ON SCREEN.** What is being
  asserted, the live measured values, and pass or fail, readable in the
  window while it runs. Not only on stdout.

## Traps that are already paid for

- **Immediate-mode text between `render()` and `present()` is silently
  erased.** `present()` clears the overlay plane first. Register a
  `ui::Label` widget instead. This cost four tests their readouts.
- **A headless test registered with `add_headless_test` links
  `logosphere_core` only** and runs in Linux CI. A window needs the full
  `logosphere` library and macOS frameworks. Giving a physics test a
  window must NOT remove it from `physics-linux`, which is a blocking
  lane.
- **Gravity is a hardcoded -Z acceleration.** Anything airborne leaves
  the frame and leaves its lights behind.
