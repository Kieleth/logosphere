<!-- Thanks for contributing to Logosphere. -->

## Summary

<!-- What does this PR do? One or two sentences. -->

## Why

<!-- What problem or need motivated it? Link issues with "Fixes #123"
or "Closes #456" if applicable. -->

## Changes

<!-- Bullet the significant edits. Not every file — just the ones a
reviewer needs to know about. -->

-
-

## Testing

<!-- How did you verify this works? Describe. -->

- [ ] Existing tests pass (`./build/logosphere-tests --no-head` + relevant standalone executables)
- [ ] Added tests for new behavior
- [ ] Built without warnings

## Checklist

- [ ] User-facing change? → entry added to `CHANGELOG.md` under `[Unreleased]`
- [ ] Ontology change? → ran `python scripts/generate_ontology.py`, committed generated files
- [ ] New module or public header? → lives in `include/logosphere/<module>/`
- [ ] docs/ARCHITECTURE.md conventions honored (no hardcoded game logic in engine, crash loud on malformed engine-internal data, defensive parsing at game/engine boundary)

## Screenshots / logs

<!-- Optional. Paste stderr output, before/after screenshots, test
results. -->
