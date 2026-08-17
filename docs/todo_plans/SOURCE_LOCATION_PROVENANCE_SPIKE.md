# Source location and provenance micro-spike

State: research completed 2026-08-17. No owner decision is recorded by
this document. It narrows the open leaf-identity decision in
`LOGOVGER_INGESTION_LEDGER_ROADMAP.md`.

## Question

Can an existing web or Web3 standard give Logovger a stable identity
for any source fragment, including repeated text, empty table cells,
PDF pages, and scanned images, while preserving who or what read it?

The short answer is: not one standard, but the pieces already exist.
The location work is mostly W3C Web Annotation and IIIF. The Web3 part
is content addressing, represented by IPFS CIDs. W3C PROV-O supplies
the derivation history. ALTO supplies a useful OCR layout interchange
format. None of them decides document structure or proves that a
reader covered the whole source.

## Existing Logovger boundary

The binding design is already explicit in `RPG_MODULE.md` and
`REFLECTION_PROTOCOL.md`:

- A model reader decides structure. A deterministic Markdown parser is
  not the authority.
- For digital text, a source tool returns the exact bytes selected by
  the reader. The reader never retypes them.
- Reading is cumulative under R11. Each reader receives the passage,
  the schema, and the relevant prior graph.
- Positive and adversarial readers are followed by one arbiter.
- The coverage ledger must account for the whole pinned source, using
  atomic leaves rather than only passages already judged to be rules.

This spike does not restore the parser that the 2026-08-15 decision
rejected. `SourceDocument` and `SourceLocator` are useful built
infrastructure and defect evidence, but their parser-created table
identity cannot be the future source of truth. The measured parser
silently loses pipe rows, splits valid irregular tables, and creates a
phantom table. The current locator also cannot identify an empty cell
because `exact` must be non-empty, and row-key lookup cannot distinguish
duplicate or blank keys.

## What the standards actually solve

| Concern | Existing work | What it gives us | What it does not give us |
|---|---|---|---|
| Immutable source representation | IPFS CID | A versioned, typed content identifier containing a content codec and self-describing cryptographic hash | A location inside the content. The same logical file may also receive different CIDs under different codecs or DAG construction. |
| A fragment inside a resource | W3C Web Annotation | A `SpecificResource` made from a source, optional state, and selectors such as byte position, text position, text quote, fragment, SVG region, and range | A canonical selector for Logovger or a completeness proof. Multiple selectors only SHOULD agree, and a consumer must pick one if they do not. That is too weak for verification. |
| A page or image region | IIIF Presentation 3 | A Canvas as a page frame, rectangular `xywh` fragments, Web Annotation selectors, and OCR or manual text as a `supplementing` annotation | Correct OCR, model agreement, or a requirement to deploy an IIIF server. |
| Derivation and responsibility | W3C PROV-O | Entities, activities, agents, generation, derivation, quotation, revision, and primary-source links | Fragment selection. PROV explicitly leaves the location vocabulary to another model. |
| OCR text and layout exchange | ALTO XML | Pages, regions, lines, words, coordinates, recognized content, and OCR metadata | Source truth. An ALTO file is an OCR result derived from an image, and it does not judge game-rule structure. |
| Location within EPUB | EPUB CFI and W3C Web Publication work | Paths and positions within EPUB or publication structures, with text assertions for recovery | A media-independent location grammar. It is useful as a format-specific selector, not as Logovger's universal identity. |

Primary references:

- [W3C Web Annotation Data Model](https://www.w3.org/TR/annotation-model/)
- [W3C PROV-O](https://www.w3.org/TR/prov-o/)
- [IIIF Presentation API 3.0](https://iiif.io/api/presentation/3.0/)
- [IPFS CID specification](https://specs.ipfs.tech/cid/)
- [Library of Congress ALTO description](https://www.loc.gov/standards/alto/description.html)
- [W3C Web Annotation Extensions for Web Publications](https://www.w3.org/TR/wpub-ann/)

## Important findings

### Content identity and fragment identity are different

A digest answers, "which exact representation?" A selector answers,
"which part of that representation?" A valid leaf identity needs both.
A hash of the leaf text alone is insufficient because duplicate
sentences and empty cells can have identical content. A path alone is
insufficient because the same path may mean different bytes after a
source revision.

Within an immutable, content-addressed representation, byte offsets are
stable. The current `SOURCE_LOCATORS.md` rejected byte offsets because
they do not survive a re-vendored source. That combines two distinct
problems. A new representation is a new source state and should get a
new identity. Relating a leaf across revisions is a separate alignment
or `wasRevisionOf` claim, not identity continuity.

### W3C selectors need a stricter Logovger profile

Web Annotation permits several selectors for the same target, but only
says they SHOULD select the same content. If they differ, a consumer
must choose one result. Logovger cannot accept that fallback. When more
than one selector is supplied, every applicable selector must resolve
to the same source fragment or verification fails.

For pinned digital text, a byte-range selector can distinguish repeated
headings, duplicate sentences, duplicate row keys, and blank cells.
A text-quote selector with exact text plus prefix and suffix is useful
secondary evidence and search support, but cannot identify an empty
leaf on its own. A structural path remains useful for display and
navigation, but cannot be the canonical identity because the reader,
not a parser, judges that structure.

### OCR text is derived evidence, not original source bytes

For a scan, the original evidence is the image or PDF representation
and the selected page region. There are no source text bytes to copy.
A small multimodal reader may perform both OCR and structural judgement,
but its transcript is a generated entity derived from the pixel region.
IIIF models exactly this distinction: the page image is associated with
a Canvas, while OCR or manual transcription is a `supplementing`
annotation targeted at the Canvas or one of its regions.

The citation must therefore retain the source artifact digest, the
page or Canvas identity, the selected rectangle or polygon, and the
exact evidence rendition used by the reader. The transcript must retain
the reading activity and reader identity that produced it. A later
reader can inspect the same pixels and disagree without changing the
source.

This exposes an open mismatch in the current protocol wording. It says
that an OCR pass may replace Markdown at L0 and still provide exact
bytes. That works only if the OCR file is deliberately declared to be
the pinned source. It does not preserve provenance back to an original
scan. If the scan is authoritative, its pixels belong in the source
chain and the OCR transcript is derived. That change requires an owner
decision before the binding protocol is edited.

### Standards do not close the coverage ledger

Web Annotation can name every leaf the readers produce. It does not
prove that they produced every leaf. R7 still needs a Logovger gate.

For digital text, a verifier can check that model-selected ranges form
a complete accounting of a declared canonical content stream, with any
syntax or whitespace exclusions typed explicitly. For an image, making
semantic regions cover every pixel is meaningless because margins and
spacing are also pixels. Page-level completion plus region-level leaves
is one possible solution, but it would weaken the current decision that
atomic leaves are the canonical coverage units. This remains a separate
design problem. The locator standard must not pretend to solve it.

## Standards-shaped contract to test

This is a candidate contract, not a decision or schema naming proposal.
It deliberately maps to the project's ontology and KG rather than
requiring external JSON-LD infrastructure.

1. A source artifact identifies the logical work or file.
2. A source representation identifies exact bytes with media type,
   byte length, digest algorithm, and digest. Its identity changes when
   any byte changes.
3. A source target combines one representation with one required
   canonical selector. A digital-text target uses a byte range. An
   image target uses a page or Canvas plus a rectangle or polygon.
4. Optional quote and structural selectors aid review. They do not own
   identity, and all supplied selectors must converge.
5. A reading activity records its input target, reader role, reader
   implementation and version, configuration or prompt digest, output,
   and generation time. Positive reader, adversarial reader, and
   arbiter remain distinct activities.
6. Copied digital text is quoted from its target. OCR text is derived
   from its image target. Neither relation replaces the target.
7. A coverage leaf is snapshot-scoped. Its tentative deterministic ID
   is a digest of profile version, source-representation identity, and
   the canonical selector. Supporting selectors and the reader's
   semantic judgement do not enter the ID.
8. Claims cite one or more coverage leaves. A scan-derived claim cites
   the image target as evidence, not only the OCR text.

The model reader still decides where a leaf begins and ends, which
visual region belongs to it, how irregular cells compose, and what the
content means. The source tool turns that decision into selectors and
returns the exact byte slice or pixel crop. The verifier validates
bounds, source digest, selector convergence, copied text, provenance,
and ledger closure. It does not decide structure.

## Consequences for the immediate TDD phase

If the owner selects a standards-shaped profile, the first red fixtures
should prove these failures before any production type is added:

- identical sentences at different byte ranges receive different leaf
  identities;
- two empty cells at different ranges receive different identities;
- changing a source byte changes the representation and leaf identity;
- a quote selector that disagrees with its byte range fails;
- an OCR transcript without a page-region target and reading activity
  fails;
- a claim citing only OCR text, with no path to the source image, fails;
- two readings of one image region remain two derived readings of the
  same source target;
- no selector or provenance type is encoded as an unchecked free-form
  string.

No code should be written from this list until the standards adoption
boundary below is selected.

## Owner decision required

### Option 1: standards-shaped local profile

Adopt W3C source, target, selector, and state concepts; IIIF page-region
and derived-OCR semantics; PROV entity, activity, and agent semantics;
and CID-style typed digests. Express only the required subset in the
existing ontology and KG. Do not require IPFS, RDF, JSON-LD, IIIF HTTP
services, or ALTO files. Add interchange adapters only when a real
second consumer needs them.

Pros: solves the current identity problem, fits the project's typed and
composable KG direction, preserves a path to interoperability, and adds
no deployment stack. Cons: the repository owns a strict profile and a
future adapter; conformance to the external wire formats is not automatic.

### Option 2: full standards serialization now

Store or emit W3C Web Annotation and PROV JSON-LD, use IIIF manifests
and Canvases for image sources, and accept ALTO where OCR providers emit
it. A CID may identify each representation.

Pros: strongest immediate interoperability and existing tooling. Cons:
substantially more schema, URI, serialization, validation, and lifecycle
machinery than the current ledger needs. IIIF also assumes HTTP(S) IDs
for its own resources. This would design for consumers that do not yet
exist.

### Option 3: local locator version 2

Extend `SourceLocator` with source digests, byte ranges, page regions,
and reader metadata, without declaring a standards profile.

Pros: smallest apparent change to current code. Cons: it recreates the
same distinctions under private names, keeps the current locator model
as the conceptual center after its parser authority was rejected, and
makes later interoperability an archaeological exercise.

The research points to option 1, but that is a recommendation, not a
recorded decision.
