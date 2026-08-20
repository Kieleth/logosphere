# Contributing to Logosphere

## Repository Map

```
src/                   Engine source code (~140 .cpp/.mm files), organized by module
include/logosphere/    Public API headers, one subdir per module (capability, events,
                       damage, kg, llm, core)
tests/                 Test suite (200+ test files)
docs/                  Reference documentation
examples/eden/         Example game — knowledge-garden showcase, exercises the KG stack
examples/logotron/     Example game — light-cycle arena with an LLM Director
examples/logogenesis/  Example game — conversational world creation via the KG-ops grammar
vendor/                Third-party code (nlohmann/json)
```

## Supported Platforms

Three build profiles (`-DLOGOSPHERE_PROFILE=<full|physics|core>`). See
[README.md "Platform"](README.md#platform) for the canonical description.

- **Full engine** (`full`, default): macOS arm64. Builds Metal shaders,
  GLFW windowing, physics, rendering, examples.
- **Headless physics** (`physics`): a render-free `Engine` (null
  platform, stubbed GPU, no GLFW) + solver + dynamics + worldgen. Runs
  the locomotion guard suite (`logosphere-physics-guards`) and smoke
  tests; gates every PR on Linux CI.
- **Headless core** (`core`; legacy alias
  `-DLOGOSPHERE_HEADLESS_ONLY=ON`): `logosphere_core` plus the pure-C++
  standalone tests on any C++17 toolchain. Linux runs in CI; Windows is
  structurally compatible but untested.

## Build and Test

### macOS (full engine)

```bash
brew install cmake pkg-config glfw

cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build --config Release

make test       # run combined harness
make eden       # build and run Eden
```

### Linux / headless

```bash
sudo apt-get install -y cmake g++ ninja-build

cmake -S . -B build-headless -G Ninja -DLOGOSPHERE_HEADLESS_ONLY=ON
cmake --build build-headless -j

./build-headless/test_capability_system    # any standalone test
```

### Running a specific combined-harness test

```bash
./build/logosphere-tests --test test_name
```

### Running a specific standalone test

```bash
./build/test_capability_system   # full-engine build
./build/test_capability_system --no-head
./build-headless/test_capability_system   # headless build, same binary, same output
```

### Adding a test

Three patterns, in preference order:

1. **Headless-safe standalone test (preferred when possible).**
   Write `tests/test_foo.cpp` with its own `int main()`. In
   `CMakeLists.txt`, add `add_headless_test(test_foo)` to the
   headless block at the top of the file. Builds in both profiles
   and runs in the Linux CI job. Used by all 12 current headless
   tests (KG, capability, damage, events, ontology, game_time,
   kg_parse, etc.).

2. **Full-engine standalone test.**
   Write `tests/test_foo.cpp` with its own `int main()`. Below the
   `LOGOSPHERE_HEADLESS_ONLY` short-circuit in `CMakeLists.txt`, add
   `add_executable(test_foo tests/test_foo.cpp)` plus
   `target_link_libraries(test_foo PRIVATE logosphere ...)` and the
   relevant macOS frameworks. Use this when the test pulls in
   physics, rendering, animation, or worldgen.

3. **Combined harness (legacy).**
   Write `bool test_foo(TestContext& ctx)`. Add an `extern`
   declaration in `src/unified_test_runner.cpp`, register it in the
   harness map, add to `run_all_tests()` in the right group, and add
   the source to `TEST_SOURCES`. Use this only when the test needs
   shared engine state across many tests. New tests should prefer
   patterns (1) or (2).

## Architecture

### Engine as library

Logosphere is a library (`liblogosphere.a`), not a standalone application. Games link against it and drive the main loop. See `examples/eden/` for the integration pattern.

### Dependency direction

```
Game → Engine → Systems → Particles (never upward)
```

- Systems never call Engine methods
- Data flows down via parameters, results return via return values
- Engine acts as data broker between systems

### Entity-first rule

Games work with entities (groups of particles), never individual particles. Use `entity_system.removeEntity(id)`, not particle-level operations.

## Coding Conventions

- **C++17**, Apple Clang
- `auto&` for references, not `auto` (which copies)
- Initialize all variables: `Particle p = {};`
- Mark non-modifying methods `const`
- Tag coordinates with their space: `world_x`, `screen_y`, `local_z`
- Technical debt: `// TODO[CODE-XXX]: Description` (codes: ARCH, PERF, CLEAN, PATTERN, RENDER, KG)

## Testing Conventions

- **Fail hard** — assertions should fail loudly, not silently degrade
- **Show measured values** — `PASS: Floor G > R — G=211 R=56`, not just PASS/FAIL
- **Thresholds from data** — every numeric threshold must come from measured values, not guesses
- **Headless is source of truth** — if headless passes but interactive fails, investigate the difference
- **Rendering changes need a visual test**: see
  [docs/VISUAL_TESTS.md](docs/VISUAL_TESTS.md) for the pattern and, more
  importantly, the traps. An A-vs-A noise floor is mandatory: re-rendering the
  identical frame moves ~30,000 pixels in this engine, so "pixels differ" is
  not a signal on its own
- Standard test positions: `(-10, -10, 20)` for camera setups
- Always call `update_lighting()` before `render()` in test setup

## Changelog

User-facing changes (new public API, behavior changes, deprecations,
removals) go into `CHANGELOG.md` under the `[Unreleased]` section when
you merge the PR. Follow the [Keep a Changelog](https://keepachangelog.com/)
format. Internal refactors that don't affect callers don't need an
entry.

Category quick reference:
- **Added** — new features or APIs
- **Changed** — behavior or API changes to existing features
- **Deprecated** — soon-to-be-removed features (still work, warn users)
- **Removed** — removed APIs or features
- **Fixed** — bug fixes with user-visible impact
- **Security** — vulnerability fixes

## Where to start

The [pinned roadmap issue](https://github.com/Kieleth/logosphere/issues/10)
maps the current efforts by lane; anything labeled
[`good first issue`](https://github.com/Kieleth/logosphere/labels/good%20first%20issue)
is scoped and mentored. Questions belong in Discussions; concrete
defects and proposals belong in issues.

## Submitting Changes

1. **Fork** the repository and create a topic branch from `main`.
2. **Make the change** honoring
   [docs/ENGINE_INVARIANTS.md](docs/ENGINE_INVARIANTS.md) and
   [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), plus the conventions below.
   New public API or bug fix means a test; user-facing changes mean a
   CHANGELOG entry.

   **Read the invariants before writing physics or animation code.** They are
   short, and most of them exist because the opposite was tried and cost weeks.
   The one that catches contributors most often: a magnitude threshold
   (`if (v.z > 1.0f)`) standing in for a state that should be named and asked
   for (`if (!is_grounded())`).
3. **Sign off with `git commit -s`** — see the DCO section. The check is
   on the commit that lands on `main`, not on your branch commits, and
   signing as you go is what carries the trailer into it.
4. **Open a pull request.** CI runs the headless-core and
   headless-physics builds and test suites on Linux, plus the
   merge-policy lane; all are required before merge. Keep the PR to one
   logical change.
5. **Review.** The maintainer reviews every PR (CODEOWNERS). Merges
   are squash merges; your branch is deleted automatically after.

Direct pushes to `main` are disabled. There is no path into the tree
that skips the checks.

### Never rebase

Squash merging is the only merge method this repository has enabled,
which means the shape of your branch history is irrelevant: it is
collapsed to a single commit when it lands. Two consequences, and the
second one is the important one.

- **Do not tidy your commits.** No interactive rebase, no amend
  chains, no local squashing. Nothing you do to them survives the
  merge, so it buys nothing and costs the risk below.
- **Never rebase, at all.** Not `git rebase`, not `git pull --rebase`,
  not "Rebase and merge". A rebase replays your commits onto a base
  they were not written against, and whatever the replay drops, or
  whatever a conflict resolution keeps from your side, is deleted
  without a word. This repository lost three already-merged pull
  requests exactly that way, with CI green throughout: a branch that
  reverts `main` still compiles and still passes its tests.

When your branch is behind, merge `main` into it:

```bash
git fetch origin main
git merge origin/main      # or: git merge --ff-only origin/main
```

The merge commit lives inside your branch and vanishes at squash time.

This is enforced, not requested. `.githooks/pre-rebase` refuses every
rebase and has no override; `.githooks/pre-push` refuses force-pushes,
pushes to `main`, and branches that delete lines which landed on `main`
after the branch started. The hooks install themselves the first time
you run `cmake -S . -B build`, or explicitly:

```bash
./scripts/install-git-hooks.sh
```

## Developer Certificate of Origin

Contributions are accepted under the
[Developer Certificate of Origin 1.1](https://developercertificate.org/).
By signing off you certify that you wrote the change or otherwise have
the right to submit it under the project license.

Add a sign-off line to your commits:

```
Signed-off-by: Your Name <your@email.example>
```

`git commit -s` adds it for you. There is no separate CLA to sign; the
grant below travels with your pull request.

**What is actually checked, and where.** The gate runs on `main`, on the
single commit your pull request becomes when it is squash-merged. Your
branch commits are not checked, which is deliberate: a branch that falls
behind `main` catches up with `git merge origin/main`, and the resulting
merge commit has no authored content, cannot be signed at creation, and
cannot be signed later without the force-push this repository refuses. A
branch behind `main` could satisfy the merge rule or the old per-commit
gate, never both.

You will not notice the difference. GitHub composes a squash message as
the pull request title followed by your branch commit messages, so a
`git commit -s` habit carries the trailer through on its own. The one way
to lose it is to rewrite the squash message in the merge box and delete
the commit log along with the trailer, and that is what the gate catches.

The checker is `scripts/check-signoff.sh`; `scripts/test-signoff-on-main.sh`
proves it refuses as well as accepts, and CI runs that test on every pull
request.

## Contribution License Grant

By submitting a contribution you (a) license it under the
[Logosphere License 1.0](LICENSE.md), (b) grant the Licensor
(Kieleth) a perpetual, worldwide, irrevocable, royalty-free
copyright license to use, modify, distribute, and relicense your
contribution as part of the Software, including under commercial
terms, and (c) grant the Licensor and all recipients of the
Software a perpetual, worldwide, irrevocable, royalty-free patent
license covering any of your patent claims necessarily infringed by
your contribution alone or in combination with the Software. The DCO sign-off
certifies you have the right to make this grant. If you cannot make
it (for example, your employer owns your work), do not submit the
contribution. This framework will be reviewed by counsel before the
first commercial royalty agreement is executed.

## License

See [LICENSE.md](LICENSE.md).
