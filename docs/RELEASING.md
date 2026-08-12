# Releasing Logosphere

How to cut a new release.

## Version scheme

[Semantic Versioning](https://semver.org) — `MAJOR.MINOR.PATCH`.

While on the `0.x` line:
- `0.x.0` — may include breaking API changes, documented in CHANGELOG
- `0.x.y` — bug fixes only, no new features, no breaking changes
- `1.0.0` — first stable release. After this, breaking changes require
  a major version bump.

## Before you cut a release

Every item must be true:

1. `main` is green — `./build/logosphere-tests --no-head` passes and
   every standalone test executable passes individually.
2. Working tree is clean — `git status` shows no uncommitted changes.
3. Every user-facing change since the last tag has a `CHANGELOG.md`
   entry under `[Unreleased]`.
4. You are on `main` at the commit you intend to tag.
5. No in-flight feature branches are mid-merge.

## Steps

### 1. Pick the version number

Look at the `[Unreleased]` section in `CHANGELOG.md`.

- Any entry under **Removed** or any `Changed` that breaks existing
  callers? → bump MINOR (e.g. `0.1.0` → `0.2.0`)
- Only `Fixed` / `Security`? → bump PATCH (e.g. `0.1.0` → `0.1.1`)
- Only `Added` (backwards-compatible)? → bump MINOR

### 1b. On a MINOR bump, move the version in two more places

The package is exported `SameMinorVersion`, so a pin on the old minor
stops resolving the moment the new one installs.

1. `project(logosphere VERSION X.Y.Z)` in the root `CMakeLists.txt`.
2. `find_package(logosphere X.Y REQUIRED)` in
   `examples/consumer-smoke/CMakeLists.txt`.

Miss the second and the `headless-linux` CI lane fails at the consumer
smoke step with "Could not find a configuration file for package
logosphere compatible with requested version", *after* every test has
passed. The tests are green and the lane is red, which reads like a
flake and is not one.

`include/logosphere/build_info.h` needs no edit: it is generated from
`git describe` at configure time and picks the tag up by itself.

### 2. Update `CHANGELOG.md`

Replace the `[Unreleased]` header with:

```markdown
## [Unreleased]

*No changes yet. Add entries here as you merge.*

---

## [X.Y.Z] - YYYY-MM-DD

<!-- paste the entries that were under [Unreleased] here -->
```

At the bottom, update the compare link:

```markdown
[Unreleased]: https://github.com/Kieleth/logosphere/compare/vX.Y.Z...HEAD
[X.Y.Z]: https://github.com/Kieleth/logosphere/releases/tag/vX.Y.Z
[previous]: https://github.com/Kieleth/logosphere/compare/vPREV...vX.Y.Z
```

### 3. Commit the changelog bump

```
git add CHANGELOG.md
git commit -m "chore(release): cut X.Y.Z"
```

The commit message prefix is `chore(release):` — lets filter in
`git log` and stays consistent.

### 4. Tag

```
git tag -a vX.Y.Z -m "Logosphere X.Y.Z — <one-line summary>

<optional longer body: headline features, breaking changes, links>
"
```

**Annotated tag** (`-a`), not lightweight. Annotated tags carry the
message and author, which GitHub shows on the release page.

### 5. Push

```
git push --follow-tags
```

`--follow-tags` pushes both the commit and the tag in one command.

### 6. GitHub release

Visit `https://github.com/Kieleth/logosphere/releases/new`, select
the `vX.Y.Z` tag, and paste the relevant `CHANGELOG.md` section as
the release notes. Mark pre-1.0 releases as "pre-release" if the API
is still expected to churn.

## After release

1. Announce where appropriate (README badge, community channels).
2. Open any planned follow-up issues for the next cycle.
3. Keep adding to `[Unreleased]` as PRs land.

## Hotfix a released version

If you need to patch an already-released version (say `0.1.0`) without
pulling in newer `main` changes:

1. `git checkout -b hotfix/0.1.1 v0.1.0`
2. Cherry-pick or apply the fix.
3. Add a `[0.1.1]` entry in `CHANGELOG.md`.
4. Follow the same commit → tag → push steps.
5. Merge the hotfix branch back into `main` so the fix is not lost.

## Pre-release tags

For release candidates or beta tags, append a suffix:

- `v0.2.0-rc.1` — release candidate
- `v0.2.0-beta.1` — beta
- `v0.2.0-alpha.1` — alpha

Match SemVer's pre-release identifier rules. Mark these as
"pre-release" on GitHub.

## Yanking a bad release

If a release is broken in a way that hotfixing doesn't help:

1. Delete the GitHub release (but **not** the tag — deleting tags
   breaks consumers who already pulled).
2. Add a clear note to the tag description on GitHub explaining
   "yanked, use vX.Y.Z+1 instead."
3. Cut a replacement release immediately.

## Changelog discipline during development

Add to `[Unreleased]` *as you merge the PR*, not later. The category
quick reference is in [CONTRIBUTING.md](../CONTRIBUTING.md).

Keep entries:
- **User-focused** — write for library users, not committers. "Add
  `emit_event` response effect" is good. "Refactor effect dispatch"
  is not.
- **Imperative mood** — "add X", "rename Y", "fix Z crash".
- **One line** — if you need more, link to the PR or a doc.

Internal refactors that are invisible to callers don't need a
changelog entry.

## Who can cut a release

For now, only the project maintainer. When the project grows beyond
one maintainer, this section gets updated.
