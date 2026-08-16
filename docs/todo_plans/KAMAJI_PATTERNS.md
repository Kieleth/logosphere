# Kamaji: patterns report

Commissioned 2026-08-16 by owner directive, after the implementation to
date was judged too thin: a `std::sort` whose comparator asks one
question. Anchored at HEAD `04c3d25`. `[E]` = read from code.

---

## 0. Two facts that govern everything below

**The 2026-08-15 inversion shrinks the problem by an order of
magnitude.** Under a single-winner ELECTION, a wrong specificity
ranking means a rule **silently never runs**, which demands a sound
ordering and pushes toward predicate-dispatch ambiguity checking. Under
ORDER (`LEDGER.md`, both-apply ruling) a wrong ranking changes the
sequence of application and which name is published. Both are visible,
neither is silent, and for accumulating effects the order does not
matter at all. **Consequence: a counted, approximate, total specificity
order (the CSS model) is defensible now where it was not before.**

**The scale, measured rather than estimated.**

| Quantity | Value |
|---|---|
| Rules in shipped (non-test) game code | **3**, one per world |
| Max rules ever co-resident in one table | **2** |
| Rules declared in YAML/JSON seeds | **0** |
| Entity types across engine schemas | 268 (324 with example games) |
| Deepest `is_a` chain | 5 (6 with packs) |
| Address chain depth in the live humanoid | **2** (`Humanoid -> Arm -> particles`) |
| Contacts per frame, worst measured | 199 |
| Chain resolution cost today | 150 ns/contact, 29.88 us/frame |

**Two live rules.** If a design's justification is "it scales to N
rules", check N against 2.

---

## 1. Verdicts

| Pattern | Verdict | Why |
|---|---|---|
| **RETE** | **Absurd, and wrong-shaped** | Three independent reasons, any one sufficient. (a) There is no persistent working memory: the contact set is replaced wholesale every frame, so the delta IS the fact base and RETE would rebuild its network each frame, strictly worse than a linear scan. (b) Join arity is fixed at 2 (self, other), and the beta network exists to optimise a multi-way join tree that does not exist here. (c) `docs/RULE_LANGUAGE.md:945-947` bans automatic firing, retraction, conflict sets and mutation ordering **by decision**, and RETE is the canonical implementation of exactly those four. 2000-4000 lines before an author sees anything. |
| **TREAT** | Overkill as an algorithm, correct as an insight | "Do not materialise partial matches; index on constants and re-join per event" IS the proposed architecture. Cite it as provenance for the 40-line hash index; do not build TREAT. |
| **LEAPS** | Not applicable | Optimises an agenda. The ruling says all matching rules fire and there is no agenda. |
| **Trie / radix** | Overkill as a structure | A trie amortises a shared prefix across many routes. With 2 routes there is no shared prefix. |
| **Predicate dispatch DAG** | Overkill to build, essential as vocabulary | See §3. |
| **CSS cascade** | **The model to follow** | See §2. |
| **Prolog first-arg indexing** | **Ship this one** | See §4. |
| **ECS archetype matching** | **The deepest structural lesson** | See §5. |
| **Interval labelling for subsumption** | Overkill AND harmful | `OntologyRegistry::extend()` adds types at runtime and games use it; interval labels break on insertion and need global relabelling. `ancestorsOf()` is already O(1). |
| **Algebraic effect handlers** | Do not import | Delimited continuations solve resuming a computation; there is no computation here, only deltas to fold. And handler nesting order determines semantics, which is the exact thing the design eliminates. Take one idea only: separate an effect's SIGNATURE from its implementation. |
| **Aspect weaving** | The negative example | AspectJ leaves two aspects on one join point undefined unless `declare precedence`. That is what an interceptor registry with optional ordering becomes. |

---

## 2. CSS is the closest analogue, and gives four lessons

The most widely shipped ordered rule cascade over a type tree in
existence. Specificity is a counted triple compared lexicographically;
ties break by document order, last wins.

1. **A counted lexicographic tuple is comprehensible, computable at
   parse time, and cheap.** Empirical validation for the key shape.
2. **Ties broken by source order work in practice.** The web did not
   need ambiguity to be a load error. Under the ORDER ruling, entity-id
   order (already the declared tiebreak) is the document-order
   equivalent and is enough.
3. **The cascade is per-PROPERTY, not per-rule.** CSS does not elect a
   winning rule; it elects a winning declaration per property. This is
   exactly what the 2026-08-15 ruling reached independently. Elect per
   single-valued channel, accumulate everywhere else.
4. **Inheritance is a separate mechanism from the cascade**, kept apart
   deliberately. Address matching runs over the `HAS_PART` chain
   (instance hierarchy); specificity runs over the `is_a` lattice (type
   hierarchy). Two different graphs. Never conflate them in one score.

**The honest negative.** Counted specificity is unsound and the web
knows it: `!important` and inline styles exist because it produced
wrong answers, and `:where()` was added purely to REMOVE specificity
from a selector. Budget for the `precedence` escape hatch. Do not
pretend the approximation is exact.

---

## 3. Specificity is a partial order, and our key is an approximation

In predicate dispatch, A is more specific than B **iff A's guard
implies B's**. When neither implies the other the methods are
AMBIGUOUS, reported at compile time. This codebase already says so in
its own comments: `with_type` against `with_part_type` is genuinely
incomparable (GEDANKEN-16), and the same crossing-sets shape is R5.

So the counted key is a **heuristic total order approximating
implication**, and it is not sound: `Humanoid/**/Hand` (2 literals, 0
filters) versus `**/Finger[owner=X]` (1 literal, 1 filter) are ordered
by the key although neither match set contains the other.

Two coherent choices: the **counted total order** (CSS, approximate,
cheap, familiar) or the **implication partial order** (Cecil, sound,
refuses more tables, needs a ~150-line subsumption checker).

**Recommendation: counted now, and add the sound check later as a LINT
that flags incomparable pairs without refusing them.** Say in the code
comment that it is an approximation by choice, so the next reader is
not misled.

---

## 4. The ontology angle: nearly free, plus one hazard

**Ordering (DIKW rung 2) is one line.** `EntityTypeDef::parent` is a
single string, so inheritance is a tree, so ancestor-set size IS depth:

```cpp
key.subsumption_depth = ontology.ancestorsOf(type).size();
```

O(1), no traversal, no new field, range 0-6, fits in 3 bits.
`OntologyRegistry` already materialises the full transitive closure at
load (`ancestors_`, `addAncestors`, `isSubtypeOf` = one set lookup).
In RDFS terms this tree already runs forward-chaining TBox
materialisation. **Kamaji must not add a reasoner; it consumes the
closure that exists.**

**Matching needs the inverse and must not call `isSubtypeOf` per
contact** (the ontology is read at load, never per frame). Instead:
index routes by their literal segment types, and at match time look up
the leaf's own bucket plus one per member of `ancestorsOf(leaf)`. At
most 6 exact-string hash lookups, zero traversal. This is the standard
"materialise the closure, expand the query over it" hybrid.

**Facets stay out of the address grammar.** They do not inherit,
explicitly and by decision, so a grammar mixing inheriting type
segments with non-inheriting facet filters would carry two inheritance
semantics in one expression.

### The R5 soundness hazard nobody had flagged

If the comparator's FIELD ORDER is data-dependent (per-hierarchy
SPECIALISE vs ENCAPSULATE), two routes in different hierarchies get
compared under different rules and the comparison **can lose
transitivity**: A < B under P, B < C under Q, C < A. `std::sort` with
an intransitive comparator is **undefined behaviour**, not merely a
wrong order.

**Fix, and it costs nothing: never write a data-dependent comparator.**
Pack the key into a single wide integer at load, choosing each route's
field bit-order by its own hierarchy's policy, then sort on the
integer. Integer comparison is a total order by construction however
each key was packed. Still arbitrary across hierarchies, but
transitive, stable and reproducible, which is what INV-27 needs.

**A third R5 residual, added here:** which hierarchy's policy applies
when a route's two addresses name types in different hierarchies?
Proposal to board, not to assume: the hierarchy containing the deepest
literal segment of `other`, since `other` is key 1.

---

## 5. The archetype cache: one structure, two jobs

The ECS lesson: **move matching off the per-event path and onto the
"world shape changed" path.** A humanoid foot hits the floor 60 times a
second with an identical `(CONTACT_OPEN, [Humanoid,Foot], [FloorTile])`
shape. Match that shape once, cache the candidate route list, and every
later occurrence is one hash lookup.

The convergence worth naming: the cache key is `(occurrence kind, self
chain, other chain)`, and the **fall-through counter** ruled on
2026-08-15 is keyed by `(occurrence kind, type pair)`. **Same key.
Build one keyed structure; the counter is a field on the cache entry.**

**Cost, stated honestly.** Property filters read a MUTABLE KG property,
so a filter-bearing route cannot be cached by type-pair alone. Keep
them in a separate list evaluated per occurrence. Getting this wrong
silently breaks the silver-bullet case, which is the headline
requirement.

**Sizing honesty:** with 2 live routes the cache saves very little
today. Build the keyed structure in the slice where the counter is
required; enable candidate caching only when a profile demands it.

---

## 6. Composition needs detection, not resolution

**Decompose every effect into typed writes against named channels,
classify each channel as accumulating or single-valued, elect only on
single-valued channels.** Then composition needs no conflict phase.

| Channel | Composition |
|---|---|
| NARRATIVE (event bus) | append; free monoid; conflict impossible |
| STATE additive | sum; commutative monoid; conflict impossible |
| FIELD (force) | vector addition; already implemented this way |
| MOMENTUM (impulse inbox) | already accumulates; bounded by cardinality |
| STATE assignment | **single-valued, needs election** |
| AUTHORITY | **single-valued, needs election** |
| STRUCTURE | **single-valued, needs election** |

Four of seven need no arbitration ever. Three need one write each.

**The snapshot rule is stratified Datalog**, and saying so upgrades it
from a preference to a theorem: stratum 1 matches (reads frame-N state,
writes nothing), stratum 2 applies (written state read by nobody until
frame N+1), and a stratified program has a unique model independent of
evaluation order.

**The accurate claim is "conflict DETECTION, no conflict RESOLUTION",**
because `state.set:<p>=<v>` names its property in an argument string,
so "do two routes write the same property" is undecidable at load when
the name is dynamic. Exactly two runtime detectors, each a hash-map
insert that fails closed.

**This doctrine is already in the tree.** `src/rules/` refuses eleven
would-be ties at load (overlapping bands, duplicate labels, duplicate
handlers, a rule carrying both a fixed and a rolled delta), and carries
a **totality proof** that walks every reachable dice total before
rolling, so match-and-no-match are both structurally impossible. That
totality proof is the model for the physics-default `else` branch:
**prove every occurrence has an answer rather than checking at
runtime.**

---

## 7. Complexity budget

| Component | Lines |
|---|---|
| `AddressPattern` parse + compile + matcher | ~120 |
| Packed specificity key + sort | ~60 |
| Bucket index with ancestor expansion | ~40 |
| `ChainCache` + invalidation | ~50 |
| `ArchCache` + fall-through counter | ~70 |
| `DeltaBuffer` + commit + two conflict detectors | ~120 |
| `RouteTable` compile + KG load + validation | ~200 |
| **Total** | **~660** |

Against 2000-4000 for RETE, before it does anything visible, for 2 rules.

---

## 8. Slice plan (Q1 = SUBSUME, so the class GROWS, never forks)

**The key that makes this cheap:** an existing `TransformationRule` with
an `ON_CONTACT` trigger **is already a Kamaji route** with `self: *`,
`other: *`, guard = its condition, effects = its effect expression,
outcome = empty. So the early slices are representation changes with
provably zero behaviour change.

- **K0. Relocate, no behaviour.** `Kamaji` in `src/interaction/kamaji.{h,cpp}` owning what the contact loop, `rules_` and `rule_order_` do today. Verification: all four existing contact tests pass untouched. "One place to look" made physical before anything is added.
- **K1. `Route` record + compiler, one route shape.** Effects pre-split at load. Today's `tier()` becomes `guard_count` in the packed key. Behaviour identical to `80a9136` by construction. Ratchet: `test_rule_order_determinism` byte-identical, both scenarios, both authoring orders.
- **K2. Chain resolution + `ChainCache`.** Depth-capped, cycle-guarded, ambiguity reported (today the code silently takes `.front()`). Zero behaviour change while the humanoid is depth 2, and that is testable. Red fixture: use `create_arm` / `create_leg`, which already build depth 3 and have **zero callers**, rather than writing new generator code.
- **K3. Address language + bucket index.** Existing rules compile to `*`/`*` and behave identically. **Decide here whether mid-pattern `**` is needed.**
- **K4. Ontology + schema.** `InteractionRoute`, the closed occurrence enum, effect kind/cardinality, the hierarchy `specificity_policy` annotation. After K3, so the schema encodes a grammar proven against real chains.
- **K5. Occurrence completion.** `CONTACT_CLOSE` / `CONTACT_SUSTAIN` from the open-set diff (the pattern already exists for volumes and contacts discard it), `MEDIUM_SUSTAIN`, `MOMENTUM_REFUSED`. No physics TU edits. Unblocks burning-hand links 5-7.
- **K6. Outcome name + physics default + counter.** GEDANKEN-13.
- **K7. Kind system + `DeltaBuffer` + snapshot.** `knockback:<speed>` becomes `knockback:<share>`, old spelling deleted. **The one slice with a real behaviour break.** CHANGELOG required.
- **K8. Delete the old path.** `ON_TIMER` stays on `TransformationRule` (a timer is a particle lifecycle event, not an interaction between two things). Migrate the three shipped rules. **Q1 = SUBSUME is satisfied here and only here**, so there is never a moment with two systems answering "what happens when things touch".
- **K9. Deferred.** Interceptors, WRAP, escalation rung 3, `slide`/`roll`/`topple`.

---

## 9. Decisions this report puts to the owner

1. **Mid-pattern `**` (AMQP form) or leading-only (MQTT form)?**
   `Humanoid/**/Hand` makes the matcher an NFA. Leading-only makes it a
   suffix compare: ~40 lines instead of ~120, and the leaf index becomes
   exact rather than a filter. Is the mid-pattern form a real
   requirement or a syntactic reflex?
2. **Load-time tie: error or warning?** With entity id in the key,
   exact ties are impossible, so the ruled load ERROR has nothing left
   to detect. The only observable tie is two routes tying above
   `entity_id` while declaring DIFFERENT `outcome:` names. A warning on
   that case would delete a mechanism. The ledger currently says error.
3. **Interceptors: cut from v1?** Redundant with registered conditions
   (`retarget` = a route with a condition backed by the combat system's
   own state; `veto` = a negated condition). The genuinely new
   capability is "change the outcome after seeing what else matched",
   and nothing in the 18 Gedankenexperimente requires it. Recommend
   deferring, not declaring dead.
4. **The two R5 residuals, plus a third**: default policy for a
   hierarchy that declares nothing; annotation on the root type or on
   every type; and which hierarchy's policy applies when a route's two
   addresses name types in different hierarchies.

## 10. What would change these recommendations

- **If single-winner election returns**, counted specificity becomes
  unsound in a way that silently kills rules, and the implication-based
  partial order with load-time refusal becomes mandatory. +150 lines
  and a materially stricter authoring experience.
- **If route counts reach hundreds** (a shipped RPG with a full
  material matrix, which is the werewolf case at scale), the bucket
  index stops being sufficient and a real trie starts to earn its
  place. Board the threshold rather than guessing it: the archetype
  cache's hit counter will say when.
