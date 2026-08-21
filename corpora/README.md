# corpora/

Vendored source text. Bytes as published, pinned to a revision, never
modified.

**This is the only thing games share.** Everything downstream of a
corpus, meaning schemas, seeds, generated headers, extractors, tests
and the prose a narrator writes, has been touched by one game's code and
belongs to that game alone. A corpus has not been touched by anything,
which is precisely why two games may read the same one without either
owning the other.

## Layout

    corpora/<corpus-name>/          the vendored tree, as published
    corpora/<corpus-name>/SOURCE_COMMIT   the upstream revision it is pinned to
    corpora/<corpus-name>/LICENSE         the terms it arrived under

Paths *inside* a corpus are the addresses seeds cite (`book1/skills.md`
and a byte range). They are stable across the move of the corpus root,
which is what makes relocating one a no-op for every citation.

## How a game reads one

A game **declares** the corpus it reads, by name. It never derives the
path from its own directory.

```cmake
logosphere_game_corpus(<target> cepheus-srd <PREFIX>)
```

That hands `<target>` a `<PREFIX>_CORPUS_DIR` compile definition holding
the absolute root, mirroring the `<PREFIX>_GAME_DIR` a game already gets
for its own data. `cmake/corpora.cmake` holds the function; an
undeclared corpus name is a configure-time failure.

**A third game is the ordinary case.** It calls the same function with
the same corpus name and gets the same bytes. Nothing new is invented:
no define, no path, no copy. This exists because the Cepheus SRD used to
live at `examples/logovger/srd/cepheus`, where a second game reading the
same book would have been a change to the first game's tree.

## Corpora vendored here

| Name | What | Pinned to |
|---|---|---|
| `cepheus-srd` | Cepheus Engine SRD, mdBook markdown conversion. Open Gaming Content; see its own `LICENSE` and `legal.md`. | `SOURCE_COMMIT` |

Currently read by `examples/logovger` (Cepheus chargen, frozen as a
milestone). The next game to read it declares the same name and gets the
same bytes.

## What stops a game reading another game instead

`scripts/check_game_isolation.py`. It asks the compiler what each game's
translation units actually open and refuses a header, a source or even
an unused `-I` belonging to a different game. `corpora/` is the sole
exception, and only when it is reached through
`logosphere_game_corpus()`. `scripts/test_check_game_isolation.py`
breaks each of those rules in a scratch repository and requires the
refusal, so the gate is known to be connected.
