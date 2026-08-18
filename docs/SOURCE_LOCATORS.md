# Source locators

_Engine facility. Address a piece of a source text, resolve it back,
and prove that captured data says what the source says._

> **IMPLEMENTATION STATUS:** This document describes the built locator
> and verifier. Its deterministic Markdown normalization is not the
> authority in the active ingestion design. Since 2026-08-15, a model
> reader judges structure and a source tool returns exact evidence. The
> current types also cannot identify all duplicate or empty atomic
> leaves. The cross-format replacement is being decided from
> `todo_plans/SOURCE_LOCATION_PROVENANCE_SPIKE.md`.

## The problem

Ingesting a rulebook turns printed text into typed data. The data is
only trustworthy if you can go back and check it, so every captured
value carries a citation. The question is what a citation *is*.

A quote is not enough, and here is the failure that proves it. This
line is one row of a career table:

```
| Qualifications | Dex 5+ | Dex 5+ | Edu 6+ | Int 6+ | End 5+ | Edu 6+ |
```

Scout is the fourth career, so Scout qualifies on **Int 6+**. Data
claiming Scout qualifies on **5+** and citing that line passed
verification, because "5" appears in the line as Pirate's `Dex 5+`.
The check was "the value's digits appear in its quote", and a row
carries six careers' numbers.

**The rule lives in a cell. A line citation cannot prove a cell.**

The same line citation also cannot say *which* table it came from. One
career block prints six different rows beginning `| 1 |`: ranks,
material benefits, cash benefits, and three skill tables.

## The idea

Borrowed from OCR, where a bounding box says which part of the page a
value came from and you can always go back and look. A **locator** is
the text equivalent: a structural address plus the text that address
is supposed to hold. Resolving it returns what the source *actually*
has there, and the two are compared.

Shaped after the [W3C Web Annotation](https://www.w3.org/TR/annotation-model/)
selectors. The built implementation deliberately excludes byte offsets
because they do not survive a re-vendored source. The provenance spike
separates source revision from location: inside an immutable,
content-addressed representation, a byte range is stable and can
distinguish duplicate text and empty cells. Human navigation can remain
a supporting structural selector.

## The two pieces

### `SourceDocument` (`logosphere/text/source_document.h`)

A source normalized once into the shapes rules actually live in:
sections (by heading, with their trail from the root), paragraphs split
into sentences, tables with columns and keyed rows, and list items.

Markdown has a parser today. The original design required every new
source format to provide another deterministic parser into this model.
That claim is superseded: the parser silently corrupted irregular SRD
tables, so it cannot define source truth for future ingestion. It
remains the implementation used by the current verifier until the
model-driven source-target contract is selected and built.

One thing the markdown parser handles that plain markdown does not: a
single run of pipe rows can hold several logical tables when the source
restates its column headers to start each one, which is how the Cepheus
career blocks are printed. A row that restates the headers opens a new
table. Sources that do not do this yield one table, as normal.

### `SourceLocator` (`logosphere/text/source_locator.h`)

```cpp
SourceLocator loc;
loc.file   = "book1/character-creation.md";
loc.path   = {"Career Tables"};        // heading trail
loc.kind   = LocatorKind::Cell;
loc.table  = "Career";                 // the table's label
loc.row    = "Qualifications";         // the row's key
loc.column = "Scout";                  // the column's header
loc.exact  = "Int 6+";                 // what it should say

auto r = resolve_and_match(doc, loc);
// r.ok    -> the source really says that, there
// r.text  -> what the source says (on failure, what it says INSTEAD)
// r.line  -> where to go and look
```

Kinds: `Cell`, `Row`, `Sentence`, `Heading`, `ListItem`.

- **Cell** is exact. `Int 6+` is not `Dex 5+`, and a wrong value cannot
  borrow a neighbour's number. Use it for anything tabulated.
- **Sentence** is for prose, matched by its text plus optional `prefix`
  and `suffix` when the sentence occurs more than once.
- **Row** is for rules that really are the whole row. A `column` may be
  supplied purely to disambiguate, since four career blocks each print
  a table labelled `Career` with a row `Qualifications`.

`path` matches on the **tail** of a section's heading trail, so a
citation need not spell out every ancestor; add ancestors when a
heading repeats.

## What resolution guarantees

- **An address that does not exist fails, and says why**, naming what
  *is* there: which tables the section has, which columns the table has.
  "No table 'Service Skills' under 'Career Tables'" is a fixable error
  message; a silent miss is not.
- **An address that exists but says something else fails**, with the
  source's own answer in the reason: `the source says 'Int 6+' there,
  not 'Int 5+'`.
- **An ambiguous address fails** rather than picking one. If two tables
  under one heading both match, that is a citation that has not said
  enough.

## Current implementation workflow

The engine ships the model and the resolver. A game ingesting a
rulebook does three things:

1. **Extract** with locators. An LLM reading a table can report the
   table label, row key and column heading as easily as it can quote a
   line, and unlike a quote those are checkable.
2. **Resolve** every locator against the parsed source. This is
   deterministic, it cannot be argued with, and it is what gates the
   data.
3. **Judge semantics separately.** Resolution proves the cell says
   `Int 6+`. It cannot prove that this cell is the *qualification*
   throw rather than something else reading `Int 6+`. That is a
   second-opinion pass, and it should stay advisory rather than
   becoming a gate, or you are back to models checking models.

## Boundaries

Nothing here knows about any rulebook, game, or genre. It knows about
documents. The engine's ingestion verifier uses it; so can anything
else that needs to prove a captured value against its source.

It is also **not retrieval**. Locators exist so captured data can be
audited and quoted. Answering "what does the book say about X" at play
time is a different job with a different tool.

## Tests

`tests/test_source_locator.cpp`, run against the vendored Cepheus SRD
rather than toy text, because the defect it closes was in real data.
The load-bearing case is `test_the_lie_that_got_through_is_refused`:
the exact claim that passed the old line-based check must fail now, and
the test also demonstrates *why* the row citation let it through.
