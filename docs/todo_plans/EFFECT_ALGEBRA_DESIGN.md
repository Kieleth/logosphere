# The effect algebra

What flows through Kamaji, and how simultaneous flows resolve.

Companion to `docs/todo_plans/INTERACTION_ROUTER_DESIGN.md`, which
designs the router's plumbing (occurrences, addresses, routes,
interceptors). This document designs the *cargo*: what an effect is,
which kinds exist, which may coexist, what operations the language
allows over them, and how a set of simultaneously-applicable effects
reduces to one outcome deterministically.

Derived from `tests/invariants/GEDANKEN.jsonl`, ten Gedankenexperimente
registered beside the invariants per the owner's method (LEDGER
2026-08-15, "Kamaji: SUBSUME (Q1 ruled), and effects must compose").
Nothing below is asserted from taste. Every rule cites the experiment
that forced it.

Sibling study: `EFFECT_COMPOSITION_INVENTORY.md` inventories how
simultaneous effects are resolved in the tree **today**. Where the two
documents touch, this one cites it. Its finding that `src/rules/`
refuses eleven would-be ties at load is the precedent §3.2 leans on,
and its N1 is a live instance of the STATE conflict class in §4.3.

Marks used throughout: **[E]** established, verified in the tree at the
cited `file:line`. **[I]** inferred, and the inference is stated.

---

## 0. The register: fields, and why each one earns its place

`tests/invariants/GEDANKEN.jsonl` mirrors `INVARIANTS.jsonl` in shape:
one JSON object per line, `id` + `slug` + `kind` + `derives_from`, so
the two files can be read, swept and cross-referenced by the same
tooling.

| Field | Why |
|---|---|
| `id`, `slug` | Same addressing as an INV. GEDANKEN-4 is citable in a commit message. |
| `kind` | `primitive` (two particles, one question), `compound` (builds on a primitive), `chain` (multi-link). The set is meant to be read simplest-first. |
| `derives_from` | Which experiments it presupposes. Same field, same meaning as an INV's. |
| `setup` | The scene, in numbers. Masses, speeds, materials. An experiment whose setup cannot be typed into a test is not an experiment. |
| `question` | Exactly one, and it is the reason the experiment exists. |
| `expected` | What a real engine must produce **and the physical reasoning**. This is the field that does the work: it is where conservation is checked before anything is designed. |
| `effects_required` | Named effects that must be in play. Empty is a legal and important answer (GEDANKEN-1). |
| `composition` | What happens when they combine, and what goes wrong under naive handling. The naive half is not decoration: it is the failure the algebra must make unwritable. |
| `invariants` | The INVs that constrain the answer. Every experiment is bounded by laws that already exist. |
| `carrier` | What in the tree today produces each piece, `file:line`, marked `[E]` or `[GAP]`. The INV schema's `mechanism` field, adapted. |
| `forces` | The algebra rule this experiment forces. The link from Part 1 to Part 2, so no rule below is orphaned. |
| `status` | `settled` or `open`. Six settled, four open. |
| `notes` | The residual, the honest caveat, the thing found on the way. |

Two INV fields are deliberately absent. `verification` does not apply:
a Gedankenexperiment is not verified, it is *reasoned*, and its
verification is the test the algebra later ships. `origin` collapsed
into `notes`, because for six of the ten the origin is the same line of
the same ledger entry.

---

## 1. What the experiments found

Five findings changed the design. Three are defects, two are structure.
All five are measurements or direct code reads, not opinions.

**F1. `restitution` has no reader. [E]** `PhysicsConfig::restitution`
is declared at `include/logosphere/physics/physics_system.h:53` and
grepping `restitution` across `src` and `include` returns exactly that
one line. Every free-pair contact in the v4 solver is perfectly
inelastic. **Consequence for the algebra:** `bounce` is a *material*
outcome that the engine cannot currently produce, so a route naming
`bounce` names something that does not happen. The burning-hand chain's
link 6 ("hand bounces") therefore has no physical source today except
stored bond strain or the position-repair push-out, neither of which is
a restitution model. This is a gap for the board, not for the router.

**F2. An impact cannot break a weld, by construction. [E]** Welds tear
on `force = impulse/dt` at or above the threshold for **12 consecutive
frames** (`src/core/physics_system_v4.cpp:4396-4419`). GEDANKEN-3's
bullet delivers 24 N against a 700 N nail, and a strike thirty times
larger still clears the bar for one frame out of twelve. The ledger
recorded the general form on 2026-08-15: an impulse cannot break a
nail, a weight can. **Consequence:** the engine has no mechanism by
which a fast light body breaks anything. Either the tear law gains an
energy criterion (physics) or fracture becomes an authorable effect
(game). That is a fork, and it is Q3 below.

**F3. EXTERNAL authority is the last pre-solve guess, and it
over-delivers by a measurable factor. [E] + [I]**
`inv_mass_momentum` returns 0 for `KINEMATIC`
(`src/core/physics_system_v4.cpp:532-537`), and the contact row is
built from it (`:1381-1382`). **[E]** So in GEDANKEN-4 the row's
effective mass is the trunk's alone, and cancelling a 1.5 m/s approach
costs `200 * 1.5 = 300 kg*m/s`, all of it loaded onto the trunk, which
leaves at 1.5 m/s. Priced honestly the impulse is the reduced mass
times the approach, `(70*200/270) * 1.5 = 77.8 kg*m/s`, giving the
trunk 0.389 m/s. **The trunk receives 3.9x the honest impulse,
concentrated into one frame instead of the tenth of a second a real
stop takes.** **[I]**, and the inference is arithmetic over the cited
lines. The comment at `:515-526` makes exactly this argument for the
sleep cache and calls the alternative a guess:

> "a sleeping body stops claiming immovability. It is priced at its
> TRUE mass, the solver computes the interaction honestly, and the wake
> decision is the RESULT."

INV-31 made that a law for sleep. `KINEMATIC` is the same shape and did
not get the same treatment. **This is the single largest finding in the
study and it is a physics item, not a router item.** See Q7.

**F4. The election unit is (occurrence, side), not occurrence. [E]**
A contact is already evaluated once per side with the normal
re-oriented so each side reasons about itself
(`src/interaction/particle_interaction_system.cpp:238-262`, `:356-370`;
contract at `include/logosphere/interaction/contact_response.h:38-66`).
GEDANKEN-9 shows the consequence: "I was blocked" and "I passed
through" are statements about two different bodies and are both true.
There are two elections, not one contest. A whole imagined class of
conflict does not exist.

**F5. "Silver bullet AND the werewolf is on fire" is two occurrences,
not two claims. [I]** The fire is a medium acting on the pair
(werewolf, fire); the bullet is a contact on the pair (werewolf,
bullet). Different pairs, so INV-22 never binds them together. What
they can contend for is only what the *body* has one of: its motion
authority, and any single property both want to write. This reframing
is what makes the resolution procedure small.

---

## 2. The taxonomy of effects

Six kinds. Each is defined by **what conserved or single-valued thing
it touches**, because that is what decides whether two of them may
coexist. Not by what subsystem implements it.

| Kind | Touches | Shape | Coexistence |
|---|---|---|---|
| **MOMENTUM** | the momentum an occurrence carried | impulse deposited into an inbox | at most one per (occurrence, side) |
| **AUTHORITY** | who owns a body's motion next frame | a state change on the body | at most one per (body, frame) |
| **STATE** | a KG property | a write, or an addition to an accumulator | many, if disjoint or additive |
| **NARRATIVE** | nothing physical | an event on a bus | unlimited |
| **FIELD** | a body's velocity, continuously | a force integrated per frame | unlimited, additive |
| **STRUCTURE** | topology: bonds, particles, profiles | an irreversible edit | at most one per (target, frame) |

### 2.1 Why the split falls exactly there

**MOMENTUM is single-assignment because momentum is conserved.**
GEDANKEN-8: one contact carrying 0.400 kg*m/s, two routes each
depositing `knockback:0.6`, gives 0.48 kg*m/s delivered, 120 % of the
momentum and 144 % of the energy on a 1 kg body. Two ways to hold the
bound exist. A shared budget makes the outcome depend on draw order, so
the language would have to define a draw order and every author would
have to reason about it. Single-assignment leaves nothing to order.

**AUTHORITY is single-assignment because the question has one answer.**
GEDANKEN-5: `absorb` and `release` are two answers to "who owns this
body's motion next frame". INV-7 says that question has exactly one
answer; INV-22 says one mechanism owns a pair. Running both delivers
the strike twice, once through the solver taking the released body and
once through the writer spending the booked impulse.

That two independent experiments arrive at the same property is the
argument for making it a property of a **kind** rather than a rule
about `knockback`. **[I]**

**STATE accumulates, with one exception.** GEDANKEN-6: burning at
5 J/s and a silver wound of 3 J simply add, because damage is a numeric
accumulator and addition is commutative. Two writes of the same
property are idempotent when they agree and a conflict when they do
not. There is no third case.

**NARRATIVE is unlimited because it is not physical.** Two emits are
two events, which is correct: an event bus records that a thing was
observed twice.

**FIELD is unlimited because forces superpose.** GEDANKEN-2: drag,
buoyancy and a directional field all act on the same body in the same
frame and vector addition is associative and commutative
(`src/interaction/particle_interaction_system.cpp:644-698`). **[E]**
Fields need no arbitration at all. They are also not selected per
occurrence: a field is a standing policy on an interaction profile,
which is why it composes so freely.

One honest limit on that freedom. A body inside two media today
receives both drags, both buoyancies and both field forces, summed
(`src/interaction/particle_interaction_system.cpp:695-696`; **[E]**,
`EFFECT_COMPOSITION_INVENTORY.md` §3.9). Superposition is correct for
fields that genuinely coexist, such as gravity plus buoyancy plus a
current. Two *media* occupying one volume is a scene-authoring error,
because a point cannot be both mud and water. The algebra does not
detect it and should not pretend to: the detection belongs to whatever
validates the scene, not to the effect language.

**STRUCTURE is single-assignment and irreversible.** A bond is severed
once. A particle is deleted once. A second edit in the same frame is
operating on a world the first already changed.

### 2.2 The exclusion nobody has to enforce

Rigid contact and medium are **already** mutually exclusive for a pair,
by construction: `should_contact` sends a pair to one or the other and
a filtered pair never reaches the narrow phase
(`include/logosphere/interaction/particle_interaction_system.h:11-19`).
**[E]** GEDANKEN-2's lesson is to copy that pattern rather than the
runtime-check pattern: **where two kinds must not coexist, make the
combination unrepresentable, do not check for it.**

### 2.3 The coexistence matrix

Read as "may these two run on the same body in the same frame".

|  | MOM | AUTH | STATE | NARR | FIELD | STRUCT |
|---|---|---|---|---|---|---|
| **MOM** | one per occurrence | **NO** | yes | yes | yes | shared budget |
| **AUTH** | **NO** | one per body/frame | yes | yes | yes | yes |
| **STATE** | yes | yes | disjoint or additive | yes | yes | yes |
| **NARR** | yes | yes | yes | yes | yes | yes |
| **FIELD** | yes | yes | yes | yes | additive | yes |
| **STRUCT** | shared budget | yes | yes | yes | yes | one per target |

Two cells need their reasoning stated.

**MOMENTUM x AUTHORITY = NO.** Releasing authority means the solver
takes the body and delivers the booked impulse next frame. Absorbing
means the writer spends it. Depositing a knockback on top of either
delivers the same momentum twice. **[I]**, from GEDANKEN-5's
composition field.

**MOMENTUM x STRUCTURE = shared budget.** GEDANKEN-3: if a fracture is
paid for, the separation work plus the fragments' kinetic energy comes
out of the striker's 8 J. A shatter effect that removes the bond *and*
deposits a full knockback on both fragments creates energy. Both draw
on the same occurrence, so both go through WRAP with shares that sum to
at most one.

**MOMENTUM x MOMENTUM across different occurrences is legal, and this
is not a loophole.** A body in a pile with twenty contacts has twenty
occurrences, each carrying its own impulse; twenty `knockback:1.0`
effects deliver exactly the sum of what physics computed. The budget is
per occurrence because the momentum was. **[I]**

---

## 3. The operators

Five. Each is stated with the algebraic property that makes resolution
deterministic and explainable, because those properties are the whole
reason to have an algebra rather than a pile of rules.

Notation: an occurrence `o` carries measures `M(o)` (impulse, normal,
approach speed, contact point, booked refusal). A side `s` is one of
the two parties. `W` is the world snapshot at harvest. `C(o,s)` is the
candidate route set. A *delta* is a set of typed writes.

### 3.1 FILTER — build the candidate set

```
C(o,s) = { r : r.on = kind(o)
             AND match(r.self,  chain(s))
             AND match(r.other, chain(not s))
             AND guard(r, M(o), W) }
```

Spelled two ways: declaratively as a route's address patterns and
`when:` clause, imperatively as an interceptor's `veto`.

- **Associative, commutative, idempotent.** It is a conjunction of pure
  predicates, equivalently an intersection of sets. Adding the same
  filter twice changes nothing; the order of filters cannot change the
  result.
- **Purity is a requirement, not an observation.** A guard that wrote
  state would break commutativity immediately. Guards read `W` and
  `M(o)` and nothing else.
- **Guards are unit-constrained.** Only mass-uniform quantities:
  velocities, or momenta divided by the mass they act on (INV-10,
  verbatim). `impact_above:<m/s>` complies
  (`src/interaction/contact_response.cpp:82-88`). **[E]**

### 3.2 JOIN — elect one meaning

```
elect(o,s) = max_< { r in C(o,s) : r.claim = CLAIM }
```

where `<` is the specificity key computed at load (router design
§3.3.3: other-specificity, then self-specificity, then guard count,
then declared precedence).

- **Associative, commutative, idempotent.** It is a maximum over a
  totally ordered set, which is a join in a semilattice. The result
  does not depend on the order the candidates were presented in, which
  is exactly the property that makes the current `unordered_map`
  iteration defect (`particle_interaction_system.cpp:364-369`,
  `particle_interaction_system.h:335`) **[E]** stop mattering.
- **Ties are a load error, and the reason is stronger than a style
  rule.** With a tie the order is not total, the set of maximal
  elements has more than one member, and `max` is **not a function**.
  The operator is undefined, not merely awkward. INV-27 forbids
  resolving it by chance; the algebra forbids the resolution existing.
  This is not a new doctrine in this tree: `src/rules/` already refuses
  **eleven** distinct would-be ties at load, each naming the offending
  entities (overlapping lookup bands, duplicate step indices, duplicate
  route labels, two handlers for one type). **[E]**, catalogued in
  `EFFECT_COMPOSITION_INVENTORY.md` §4.1. Kamaji adopts the same
  doctrine rather than inventing one.
- **Identity.** `elect` over an empty candidate set is `none`: no
  outcome, no effects, and physics keeps what it already did. This is
  GEDANKEN-1's null case and it must be bit-identical to a build with
  no router in it.

### 3.3 SEQ — apply consequences in order

```
apply(delta_list) = fold_left(apply_one, world, delta_list)
```

- **Associative** (it is a fold). **Not commutative** in general: a
  state write and a state read are order-sensitive. **Not idempotent**.
- **Identity is the empty list.** `(Effects, SEQ, empty)` is a monoid.
- The order is the total order from JOIN: the winning CLAIM route's
  `do:` list first, then each OBSERVE route's list in specificity
  order.
- **SEQ's non-commutativity is contained by the snapshot rule (§4.1).**
  Because every condition in the frame was evaluated before any write
  committed, no fold order can change *which* effects run. It can only
  change the order writes land, and for the kinds that may coexist
  (STATE-additive, NARRATIVE, FIELD) the result is order-independent
  anyway.

### 3.4 MERGE — accumulate what does not conflict

```
d1 || d2   defined iff  writes(d1) and writes(d2) are disjoint,
                        or they agree on every shared target,
                        or every shared target is an additive accumulator
```

- **Associative and commutative on its domain.** Outside its domain it
  is not "last writer wins", it is **undefined**, and an undefined
  merge is a reported conflict.
- **Idempotent only for constant-valued state writes.** `state.set:
  burning` twice is once. `damage +3` twice is six, and that is
  correct.
- MERGE is a **partial** operator on purpose. Making the partiality
  explicit is what turns "two systems disagreed" from a silent race
  into a diagnostic.

### 3.5 WRAP — capture the effect, then operate on it

The owner's encode/decode. WRAP binds the occurrence's measured
quantities to read-only names in the scope of an effect, and permits
only **contracting** expressions over them.

```
wrap[share = L](e)     L in [0,1]
```

- **Composition is multiplication:** `wrap[L](wrap[M](e)) =
  wrap[L*M](e)`. So nested wraps form the commutative monoid
  `([0,1], *, 1)`.
- **Identity** is `wrap[1] = id`.
- **Contracting, and this is the guardrail expressed as algebra:**
  since `L*M <= min(L,M) <= 1` for `L,M` in `[0,1]`, **no sequence of
  wraps can produce a share above 1.** An effect cannot fabricate
  momentum, not because a check forbids it but because the operator has
  no way to express it. There is no addition in the wrap language, and
  no slot for an absolute magnitude.
- **Permitted expressions:** multiply by a constant in `[0,1]`; project
  onto the contact normal; split into named shares that sum to at most
  1. **Forbidden and unspellable:** any absolute quantity, any sum of
  measures, any expression whose value can exceed the measure it came
  from.
- **What WRAP is for, concretely.** GEDANKEN-3: capture the bullet's
  0.4 kg*m/s once, spend 0.3 of it on separating the bond and 0.7 on
  the fragments, and the arithmetic closes by construction.

### 3.6 What is deliberately absent

There is no **retry**, no **loop**, no **spawn-a-route** operator. Each
would break §4.4's termination argument, and none is needed by any
experiment. A chain (GEDANKEN-7) advances one link per frame using
nothing but the five operators above.

---

## 4. The resolution procedure

### 4.1 The snapshot rule

> Every condition evaluated in a frame reads the world as it stood when
> that frame's occurrences were harvested. Every write commits after
> all elections are complete. An effect's writes are visible to the
> **next** frame's snapshot and never to this frame's matching.

Forced by GEDANKEN-6. The hazard is exact: let a fire route write
`material=charred` and a contact route condition on
`self material=charred`. Under sequential semantics the answer depends
on which queue was drained first, which is an accident of engine
plumbing. Under the snapshot rule the contact route does not match this
frame and does match the next one, whatever the harvest order was.

The rule buys three things at once: matching becomes a pure function of
the frame's start state; JOIN and FILTER keep the properties claimed in
§3; and termination becomes a one-line argument (§4.4). It costs one
frame per chain link, which GEDANKEN-7 shows the chain was already
paying: links 3-4, 4-5 and 6-7 each need a physics step regardless.

### 4.2 The nine steps

1. **Snapshot.** Freeze the state conditions will read, at the router's
   frame point (`src/core/engine.cpp:1248`). **[E]** for the point.
2. **Harvest.** Drain the frame's occurrence queues. Each occurrence
   carries its measures. Order is declared (occurrence kind, then
   stable particle-id pair), so a rerun harvests identically.
3. **Ground.** For each occurrence, for each side, resolve the address
   chain. This is the term the routes unify against:
   `interaction(Kind, SelfChain, OtherChain, Measures)`.
4. **FILTER.** Patterns unify, property filters and guards evaluate
   against the snapshot. Pure. No writes.
5. **JOIN.** Elect the single CLAIM winner per (occurrence, side). The
   outcome name for that side.
6. **Intercept.** Stage-ordered interceptors may `veto` (re-elect over
   a strictly smaller candidate set) or `retarget` (rename only).
7. **Assemble.** Winner's `do:` list, then all OBSERVE lists in
   specificity order. Kind-check here (§4.3).
8. **WRAP and bind.** Evaluate effect arguments against the measures.
   Shares checked against the occurrence's budget.
9. **Apply (SEQ) and emit.** Fold the deltas, commit the writes, emit
   one `InteractionEvent` per (occurrence, side).

### 4.3 Conflict, and the theorem that removes most of it

> **Theorem.** If single-assignment kinds (MOMENTUM, AUTHORITY,
> STRUCTURE) are legal **only on CLAIM routes**, and JOIN elects
> exactly one CLAIM route per (occurrence, side), then at most one
> effect of each single-assignment kind can run per (occurrence, side).
> Conflicts within an occurrence are unwritable.

The proof is one line: OBSERVE routes carry no single-assignment
effects by a load-time rule, and there is exactly one CLAIM winner. The
only remaining check inside a `do:` list is that it names each
single-assignment kind at most once, which is also a load-time check.
**[I]**

This changes GEDANKEN-8's answer for the better. The two-knockback
table is not a runtime budget race, it is a **route table that refuses
to load**, because the OBSERVE route carries a MOMENTUM effect.

What survives is exactly two runtime conflict classes, both **across**
occurrences on one body in one frame:

- **AUTHORITY conflict.** Two strikes in one frame, both electing an
  authority change. One answer must win. Who? See Q2. Until it is
  ruled, the honest behaviour is to report and refuse both, leaving the
  body's authority unchanged, which is the state physics already
  assumed while solving.
- **STATE conflict.** Two occurrences writing different values to one
  property. Report, refuse both, fail closed. Agreeing writes are
  idempotent and are not a conflict.
  **This class is live in the tree right now.** Two `SWAP_PROFILE`
  rules matching one episode both write `interaction_profile_id`
  (`src/interaction/particle_interaction_system.cpp:524-525`, fired
  from the loop at `:554`), and the winner is whichever the
  `unordered_map` visits last. **[E]**, found by
  `EFFECT_COMPOSITION_INVENTORY.md` N1. It is a hash order deciding an
  outcome, not merely an order, which is the sharper form of the defect
  the router design already noted.

Fail-closed on conflict follows the repo's existing door discipline:
undeclared or contradictory data refuses loudly rather than picking
(`physics` skill, "Doors, not fallbacks").

### 4.4 Termination

Four facts, and together they are the proof.

1. The installed route set is finite, so `C(o,s)` is finite.
2. `veto` only removes candidates, so re-election runs at most
   `|C(o,s)|` times.
3. No effect creates an occurrence or a route in the same frame.
4. By the snapshot rule, no write can change what matched this frame.

Therefore per-frame work is bounded by `occurrences x candidates` and
the fixed point is reached in one pass. **[I]**

**Stated honestly: cross-frame cycles are still possible.** A burns B,
B burns A, forever. That is a livelock in the *content*, not in the
engine: each frame terminates. The engine's obligation is a counter and
a diagnostic, not a cycle detector. Anything more would require reading
the game's intent.

### 4.5 Is this "the prolog way"?

Partly, and the honest description matters more than the flattering
one.

**What is Prolog-like.** A route reads as a Horn clause and matching is
unification of an address pattern against a resolved chain:

```
outcome(seared) :- occurrence(contact_open),
                   self(werewolf/**),
                   other(**/bullet[material=silver]),
                   impact_above(5).
```

The JOIN is a **cut**: the most specific clause commits and the rest
are not tried.

**What is not.** There is no backward chaining and no search. This is
forward matching over a finite, externally supplied fact set, with a
total order on clauses. And it is deliberately *better* than Prolog on
the one axis this project cares about: Prolog's clause order is the
tiebreak, so reordering a file changes behaviour. Here the tiebreak is
specificity computed from the clause's own content, so the route table
can be reordered, split across files, or assembled by a meta-agent at
runtime without changing a single outcome. That is what INV-27 needs
and what first-clause-wins could never give.

### 4.6 Reconciliation with the rule-language ruling

`docs/RULE_LANGUAGE.md:945-947` bans production-rule semantics **by
decision**: **[E]**

> "This boundary avoids an implicit production-rule loop. Automatic
> firing, retraction, conflict sets, and mutation ordering are not part
> of the selected language model."

That ruling must be honoured or explicitly superseded, and this design
honours it. Four points, each mechanical rather than a promise.

1. **No automatic firing from graph state.** A route fires on an
   OCCURRENCE, which is a physics fact pushed onto a queue, drained
   once per frame and cleared. It never fires because a predicate
   became true in the KG. The engine already draws exactly this line
   and names it: rules of this family are PUSHED, capability response
   rules are PULLED, and confusing them is what issue #36 exists to end
   (`include/logosphere/interaction/particle_interaction_system.h:
   111-115`). **[E]** Kamaji stays entirely on the pushed side.
2. **No implicit loop, and this is the snapshot rule's second job.**
   Because a write cannot be seen by this frame's matching (§4.1), a
   mutation can never cause another route to fire within the frame.
   The loop the ruling bans is not merely discouraged here; it is
   arithmetically impossible (§4.4).
3. **No retraction and no agenda.** Nothing is asserted into a working
   memory and nothing is withdrawn from one. `veto` removes a candidate
   from one election before any write happens; it does not undo an
   applied effect.
4. **Conflict is detected, not resolved.** The ruling's objection to
   conflict sets is to a resolution strategy that silently picks. Here
   the only conflicts that survive §4.3's theorem are reported and
   refused, fail-closed, which is the same answer `src/rules/` gives
   eleven times over.

**The expression layer stays pure**, as the ruling requires: guards
read the snapshot and `M(o)` and write nothing (§3.1). The one thing
this design adds that the ruling does not anticipate is **specificity
as a total order over clauses**, which is a selection rule rather than
a firing rule. It is put to the owner as Q8 rather than assumed to be
covered, because it is the single place where a later reader could
mistake this for the model the ruling excluded.

### 4.7 Worked trace: the silver bullet, and the werewolf is on fire

Scene. `W` a werewolf body particle, property `state=burning` set 30
frames ago on entering a fire volume, still inside it, `material=flesh`.
`Bl` a bullet, `material=silver`, striking `W` at 12 m/s. `F` the fire
medium.

Route table:

```yaml
Rf: on MEDIUM_SUSTAIN  self "Werewolf/**"  other Fire
    outcome burning
    do "damage.accumulate:5.0{J/s}, state.set:material=charred"
    claim CLAIM

R1: on CONTACT_OPEN  self "Werewolf/**"  other "**/Bullet[material=silver]"
    when impact_above:5
    outcome seared
    do "damage.accumulate:3.0{J}, emit_event:silver_burn"
    claim CLAIM

R2: on CONTACT_OPEN  self "Werewolf/**"  other "**/Bullet"
    outcome deflected
    do "emit_event:ricochet"
    claim CLAIM

R3: on CONTACT_OPEN  self "Werewolf/**[material=charred]"
    other "**/Bullet[material=silver]"
    outcome shattered
    do "bond.break"
    claim CLAIM

Ro: on CONTACT_OPEN  self "Werewolf/**"  other "*"
    do "emit_event:combat_log"
    claim OBSERVE
```

**Step 1-2. Snapshot and harvest.** `W.material = flesh`,
`W.state = burning`, `Bl.material = silver`. Two occurrences:
`o1 = MEDIUM_SUSTAIN(W, F)`, `o2 = CONTACT_OPEN(W, Bl)` with
`approach = -12.0 m/s`.

**Step 3. Ground.** `chain(W) = [Werewolf, Torso]`, `chain(Bl) =
[Bullet]`, `chain(F) = [Fire]`. Four election units: `(o1,W)`,
`(o1,F)`, `(o2,W)`, `(o2,Bl)`.

**Step 4. FILTER on `(o2,W)`.**
- `R1` matches. Other pattern has one literal segment plus one property
  filter; guard `impact_above:5` sees `|-12.0| >= 5`, true.
- `R2` matches. One literal segment, no filter, no guard.
- `R3` does **not** match. The snapshot says `W.material = flesh`.
  `Rf`'s write of `charred` has not committed and will not be visible
  until the next frame. **This is the whole point of the snapshot
  rule:** without it, whether `R3` fires depends on which queue drained
  first.
- `Ro` matches, `claim = OBSERVE`.

**Step 5. JOIN over `{R1, R2}`.** Key comparison, other-specificity
first: `R1` has (1 literal, 1 filter), `R2` has (1 literal, 0 filters).
`R1` wins on the second component. **Outcome: `seared`.** `R2` fires
nothing. No tie, so `max` is a function and the election is a join.

**Step 6. Intercept.** A combat system registered at stage 10 observes
and does not retarget. Outcome stands.

**Step 7. Assemble.** `[damage.accumulate:3.0{J},
emit_event:silver_burn]` then `[emit_event:combat_log]`. Kind check:
one STATE-additive, two NARRATIVE. No single-assignment kind present,
so no conflict is possible. Note that `Ro` *could not* have carried
`knockback` even if its author wanted to: an OBSERVE route with a
MOMENTUM effect fails at load.

**Step 8. WRAP.** No MOMENTUM effect, so no budget is drawn. Unit check
on `3.0{J}`: `CONTACT_OPEN` is an episodic occurrence, the effect is
registered `ONCE_PER_EPISODE`, the argument carries no time in its
denominator. Consistent.

**Step 9. Apply and emit.** `InteractionEvent{side: W, other: Bl,
outcome: seared, effects: [damage.accumulate, emit_event]}`.

**Same frame, `(o1,W)`.** `Rf` is the only candidate, elected trivially,
outcome `burning`. `damage.accumulate:5.0{J/s}` is registered
`PER_FRAME_RATE`, so it contributes `5.0 * dt = 0.0833 J`.
`state.set:material=charred` is queued.

**MERGE across the two occurrences on `W`.** Damage accumulator:
`3.0 + 0.0833 = 3.0833 J`. Addition is commutative, so the total is
independent of harvest order, and no arbitration was needed. The
`material` property was written by exactly one route, so it is a single
assignment and not a conflict.

**Commit. Next frame.** The snapshot now reads `W.material = charred`.
If the contact episode is still open, `R3` becomes a candidate and now
outranks `R1` (two property filters against one). If the episode
already closed, `R3` never fires. Either way the behaviour is visible
in the route table rather than emergent from queue order.

---

## 5. The vocabulary

Written to be lifted into `schema/logosphere.yaml` unchanged. Each
interaction is defined by **what is measured**, never by what code
runs. Names a game invents are equally legal in the `outcome` slot; the
enum is a floor, exactly as `TransformationEffect` already is
(`schema/logosphere.yaml:300-311`). **[E]**

### 5.1 Occurrences (CLOSED: each is a queue the engine owns)

| Name | Physical meaning | Status |
|---|---|---|
| `CONTACT_OPEN` | a pair began exchanging rigid contact | **[E]** `particle_interaction_system.cpp:309-319` |
| `CONTACT_SUSTAIN` | the episode persists this frame | **[GAP]** derivable from `open_contacts_` |
| `CONTACT_CLOSE` | the episode ended | **[GAP]** the set is overwritten without a diff, `:374` |
| `MEDIUM_ENTER` | a body entered a declared medium | **[E]** `:173-185` |
| `MEDIUM_SUSTAIN` | the body is inside it this frame | **[GAP]** the force pass runs, no event |
| `MEDIUM_EXIT` | the body left it | **[E]** `:187-199` |
| `MOMENTUM_REFUSED` | an authority refused an impulse physics computed, and it was booked | **[E]** as a ledger (`physics_system_v4.cpp:607-624`), **[GAP]** as an occurrence |
| `BOND_SEVERED` | a constraint reached its tear criterion and was removed | **[E]** as a mechanism (`:4417`), **[GAP]** as an occurrence |

### 5.2 Interactions (FLOOR: measured definitions)

| Name | Measured definition |
|---|---|
| `touch` | `CONTACT_OPEN` with approach speed magnitude below the route's declared bound. An exchange too small to change either body's motion perceptibly. |
| `impact` | `CONTACT_OPEN` at or above that bound. A momentum exchange large enough to change motion. |
| `rest` | `CONTACT_SUSTAIN`, relative speed within the quietness bound. The contact does zero work and the solver performs no corrective work (INV-24). |
| `slide` | `CONTACT_SUSTAIN`, tangential relative speed non-zero, normal relative speed near zero. Coulomb friction is dissipating at the interface and the loss is booked to the friction bucket. |
| `roll` | `CONTACT_SUSTAIN` where the material point at the contact has near-zero velocity while the body's angular velocity does not: `v_com + omega x r_contact ~ 0`. Rolling without slipping; no frictional dissipation. **[GAP]**, needs angular state at the seam. |
| `separate` | `CONTACT_CLOSE`. The constraint is no longer active. |
| `bounce` | `CONTACT_CLOSE` with positive normal relative velocity, at most the pre-contact closing speed (INV-17). Elastic strain energy stored during the contact was returned. **[GAP]**, see F1. |
| `stop` | The striker's normal velocity reaches zero during the episode and does not reverse. A perfectly inelastic exchange. |
| `block` | A `stop` in which the other body's velocity change stays within its own quietness bound, because the exchange was carried into that body's support. Not "immovable": INV-1 says only the turtle is. |
| `refused` | `MOMENTUM_REFUSED`. Carries the booked vector. |
| `immersed` / `submerged` / `emerged` | `MEDIUM_ENTER` / `MEDIUM_SUSTAIN` / `MEDIUM_EXIT`. |
| `topple` | `CONTACT_SUSTAIN` where the projection of the centre of mass **along the net force direction** leaves the region spanned by the body's active contacts, and angular velocity about the support edge is growing. Static equilibrium lost. Uses the net force direction, never a world-up axis, so it holds on a wall and in zero-g (INV-6). **[GAP]** |
| `sever` | `BOND_SEVERED`. |

### 5.3 Words that are NOT engine interactions, and why

Recorded so nobody adds them to the floor by mistake.

- **`hit`** = `impact` + declared intent. **No physical signal
  distinguishes a punch from a stumble.** Logomancers needed explicit
  attack sessions for exactly this reason, cited in the router design's
  Q6. Intent may enter only through a declared session; it must never
  be inferred from speed or from a body part's identity.
- **`penetrate`** = a striker continues through a body while the body
  resists progressively. Requires a plastic-resistance model that does
  not exist. Do not confuse with `pass_through` (a declared pair policy
  on the interaction profiles) or with tunnelling (a CCD defect, see
  GEDANKEN-1's carrier note).
- **`stick`** = `separate` prevented by a bond created at contact.
  Requires a bond-creation effect of the STRUCTURE kind. **[GAP]**
- **`burn`, `freeze`, `poison`** = STATE accumulated over a sustained
  occurrence. The engine ships the occurrence and the accumulator, not
  the meaning.
- **`retract`, `parry`, `dodge`** = drives commanded by a behaviour
  layer. Not interactions at all. GEDANKEN-7 link 2 is exactly this:
  the point in the chain where control leaves physics.

### 5.4 Effects

| Effect | Kind | Cardinality | Argument | Bound |
|---|---|---|---|---|
| `knockback:<share>` | MOMENTUM | once per (occurrence, side) | dimensionless `[0,1]` | share of the occurrence's own momentum |
| `motion.absorb` | AUTHORITY | once per (body, frame) | none | the writer drains the booked impulse |
| `motion.release` | AUTHORITY | once per (body, frame) | none | authority passes to the solver |
| `state.set:<p>=<v>` | STATE | idempotent | none | one value per property per frame |
| `damage.accumulate:<x>{u}` | STATE | `ONCE_PER_EPISODE` or `PER_FRAME_RATE` | declared UCUM unit | additive |
| `emit_event:<name>` | NARRATIVE | any | none | none |
| `medium.drag:<c>{kg/s}` | FIELD | per frame | UCUM | declared on a profile, not on a route |
| `bond.break` | STRUCTURE | once per (bond, frame) | share of the budget | see GEDANKEN-3 and Q3 |
| `profile.swap:<id>` | STRUCTURE | once per (particle, frame) | none | none |

`knockback:<share>` replaces today's `knockback:<speed>`
(`src/interaction/contact_response.cpp:157-166`) **[E]**, which is the
hole GEDANKEN-1 and GEDANKEN-8 both point at.

### 5.5 The schema shape

Three new closed enums plus one floor enum, in the style the file
already uses:

```yaml
  InteractionOccurrence:      # CLOSED. Each value is a queue the engine owns.
  Interaction:                # FLOOR. Measured meanings; games extend by name.
  EffectKind:                 # CLOSED. MOMENTUM AUTHORITY STATE NARRATIVE FIELD STRUCTURE
  EffectCardinality:          # CLOSED. ONCE_PER_EPISODE PER_FRAME_RATE IDEMPOTENT_STATE
```

`EffectKind` and `EffectCardinality` are closed because the resolution
procedure branches on them. That is the same test `TransformationTrigger`
already passes (`schema/logosphere.yaml:263-275`): a value a game could
add without engine code does not belong in a closed enum, and a value
the engine's control flow depends on does. **[E]**

---

## 6. What the language may NOT express

Six guardrails. For each: the invariant, and **how the language makes
it unwritable rather than merely discouraged.** A rule that can be
violated and then caught in review is not a guardrail.

**1. No effect may fabricate momentum or energy (INV-3, INV-17).**
The MOMENTUM kind has exactly one spelling and its argument is a
dimensionless share of a quantity the occurrence measured. There is no
slot for an absolute magnitude, so a magnitude cannot be authored, only
apportioned. WRAP composes by multiplication on `[0,1]`, so it is
contracting: no nesting produces a share above 1 (§3.5). A literal
share above 1 is refused at load. And by §4.3 at most one MOMENTUM
effect runs per (occurrence, side), so shares cannot be summed by
accident.
*Contrast today:* `knockback:<speed>` accepts an absolute speed
(`contact_response.cpp:157-166`) **[E]**, which is why GEDANKEN-1's
naive route can deliver 5000 kg*m/s from a 0.4 kg*m/s event.

**2. No condition may compare raw impulses across masses (INV-10).**
Conditions register with a declared UCUM unit. The registry accepts
only units whose dimension is velocity, or momentum divided by the mass
it acts on. A condition declared in `N.s` **fails to register**, at
startup, with the name in the message. This is a type check, not a
review item.
*Precedent:* `schema/physics.yaml` already carries value + UCUM unit +
group + doc for 68 physics constants under INV-29. **[E]** The same
machinery, one file over.

**3. No effect may write a position or a velocity.**
The effect context hands an effect the *system*, never the particle
array (`ContactEffectContext`, `contact_response.h:97-103`). **[E]**
The only motion-adjacent API is `deposit_impulse` into an inbox that
moves nothing by construction and that nothing drains by default
(`particle_interaction_system.cpp:265-283`, `contact_response.cpp:
143-156`). **[E]** The rule to keep it that way is mechanical: the
effect context must never gain a `WriteView`, and a static gate greps
`src/interaction/` for `WriteView`, `\.vx`, `\.x =` reachable from an
effect, ratcheted to zero. That is the shape of
`test_inv15_owner_blindness` and `test_inv29_constants_gate`, both of
which already exist and work. **[E]**

**4. No rule may make physics read a game category (INV-15).**
Three independent reasons, and any one suffices. The router lives in
`src/interaction/` and adds zero lines to `src/core/physics_*.{h,cpp}`.
It runs at `engine.cpp:1248`, downstream of the entire solve, so
nothing it computes can reach the solver this frame. And routes name
**ontology types**, which are inert with respect to physics filtering;
the only declared thing the broad phase reads is the interaction
profile's category and mask, which is a pair policy, not a game
category (`particle_interaction_system.h:11-19`). **[E]**

**5. No tie may be resolved by chance (INV-27).**
Ties are refused at load, naming both routes, and neither installs.
Not "for determinism" but because JOIN is a maximum over a total order
and a tie makes `max` not a function (§3.2). The operator is undefined,
so there is nothing to resolve.

**6. No damping and no spring as a patch (INV-5, INV-19).**
There is no effect spelling that scales a body's velocity. Velocity
relaxation exists in exactly one place, a declared medium's drag
(`particle_interaction_system.cpp:660-666`) **[E]**, which has a
dissipation story and a bucket in the energy ledger (LEDGER 2026-08-15,
C7). **[E]** The FIELD kind is declared on an interaction profile, not
on a route, so a route cannot introduce dissipation at all.

**A seventh, and it is a limit rather than a guardrail.** The language
cannot express *when* something happens inside the solve. Everything
here is post-hoc: physics has already finished the frame. A "catch"
that turns a contact into a constraint, or a "parry" that redirects
rather than absorbs, is not expressible and must not be faked with an
impulse. That is router Q7 and this study does not reopen it.

---

## 7. The TDD ladder, red first

Rungs for the **algebra**, not the router's plumbing (which has its own
ladder, router design §3.10). Each states what it measures, prints
values rather than PASS, and carries a control that proves the check
can fail.

| Rung | Claim under test | Experiment | Red today because |
|---|---|---|---|
| **A0** | **Identity.** Empty route table and no-router build are bit-identical in every position and velocity for N frames. Control: one `emit_event` OBSERVE route is also bit-identical; one `knockback` route is not. | G-1 | no router |
| **A1** | **JOIN is order-independent.** Two CLAIM candidates presented in both orders elect the same winner, byte-identical. | G-6, G-9 | rules iterate an `unordered_map`, `particle_interaction_system.cpp:364` |
| **A2** | **Ties refuse at load**, naming both routes; neither installs. Control: break the tie by one property filter and both install. | G-6 | no tie detection |
| **A3** | **Snapshot purity.** A route conditioned on a property another effect writes this frame does not match this frame and does match next frame. Control: reverse the harvest order, byte-identical result. | G-6 | no snapshot |
| **A4** | **Single-assignment is a load rule.** An OBSERVE route carrying a MOMENTUM effect refuses at load. A `do:` list naming `knockback` twice refuses at load. | G-8 | no kind system |
| **A5** | **Budget bound.** Delivered impulse for an occurrence is at most the momentum it carried; `ENERGY_LEDGER=1` shows `d_TOTAL` does not rise across a routed contact. Control: a share above 1 refuses at load. | G-1, G-3 | `knockback` takes an absolute speed |
| **A6** | **Authority is single-valued.** `absorb` and `release` on one body in one frame report a conflict and leave authority unchanged; the leak sweep reads zero. | G-5 | no authority state, INV-32 aspirational |
| **A7** | **Units and cardinality.** A condition registered in `N.s` fails to register. A `ONCE_PER_EPISODE` effect attached to a SUSTAIN occurrence refuses at load. A `PER_FRAME_RATE` effect run at two timesteps delivers the same total per second. | G-10 | arguments are unit-free strings |
| **A8** | **Two sides, two names.** The bullet's route emits `pass_through` and the wall's emits `blocked`; both events fire; positions and velocities are bit-identical to the no-route control. | G-9 | no outcomes |
| **A9** | **MERGE commutes.** Three OBSERVE routes contributing additive damage and events produce identical final state under both presentation orders. Control: two routes writing different values to one property report a conflict rather than picking. | G-6 | no merge |
| **A10** | **WRAP contracts.** `wrap[0.3]` inside `wrap[0.5]` delivers 0.15 of the occurrence's momentum. Shares summing above 1 across effects of one occurrence refuse at load. | G-3 | no wrap |
| **A11** | **Termination.** An effect that writes the state its own route reads does not re-fire in the same frame; the per-frame resolution counter is bounded by occurrences times candidates and is asserted, not assumed. | G-7 | no counter |
| **A12** | **Refused momentum round trip.** Driver into rooted body: forward progress goes to zero, `sum(booked) == sum(drained)` within 1 %, and the target's velocity change equals exactly what the solver delivered. | G-4 | no drain wired, refusal linear-only |
| **A13** | **Medium is not a contact.** Sphere in mud: zero contact rows for the pair, kinetic energy monotonically non-increasing, and the energy ledger's drag bucket accounts the loss to within 1 %. | G-2 | drag bucket exists; the assertion does not |

Two rungs are **expected to stay red until physics changes**, and that
is the point of writing them now: **A12** needs the refused ledger
complete on the angular side, and **A5**'s energy assertion is only
meaningful once EXTERNAL bodies are priced honestly (F3 / Q7).

---

## 8. Slices, dependency ordered

No estimates. Dependencies only.

**E1. The kind system.** Effects and conditions register with a kind, a
cardinality and a declared unit. Today they register a name and a
`std::function` (`contact_response.h:120-155`). **[E]** Everything
below checks something declared here, so this is the root.
*Depends on: nothing.*

**E2. Snapshot and declared harvest order.** Freeze the state
conditions read; declare the occurrence order (kind, then stable id
pair). *Depends on: nothing. Independent of E1.*

**E3. JOIN with a total order.** Specificity key computed at load,
sorted vector, load-time tie refusal. *Depends on: the router's address
language (router S3).*

**E4. Load-time kind rules.** Single-assignment kinds legal only on
CLAIM routes; at most one per `do:` list; unit-vs-cardinality
consistency. This is where §4.3's theorem becomes machinery.
*Depends on: E1, E3.*

**E5. WRAP.** Occurrence measures bound as read-only names; the share
grammar; per-occurrence budget accounting. `knockback:<share>` replaces
`knockback:<speed>` and the old spelling is deleted, not deprecated.
*Depends on: E1.*

**E6. Runtime conflict detection.** Per (body, frame) for AUTHORITY,
per (body, frame, property) for STATE. Report and fail closed.
*Depends on: E1, E4.*

**E7. The vocabulary into the ontology.** `InteractionOccurrence`,
`Interaction`, `EffectKind`, `EffectCardinality` into
`schema/logosphere.yaml`, regenerated per the ontology workflow.
*Depends on: E1 for the kind and cardinality values.*

**E8. The static gate.** `test_effect_algebra_gate`: no `WriteView` and
no direct particle field write reachable from `src/interaction/`,
ratcheted to zero, in the shape of the two gates that already exist.
*Depends on: nothing. Land it early so the ratchet never has to fall.*

**E9. Cross-occurrence authority arbitration.** *Blocked on Q2.*

**E10. The measured interactions that need physics data out.** `slide`,
`roll` and `topple` need tangential relative speed, contact-point
velocity and the active-contact support region exported from the solve.
Data out only, no policy in, so INV-15 is safe, but each is an edit to
a physics TU. *Depends on: nothing above; additive; blocks nothing.*

Dependency shape in one line: `E1 -> {E4, E5, E7}`, `E2 -> nothing`,
`E3 -> E4`, `{E1,E4} -> E6`, `E8` and `E10` free-standing, `E9` blocked
on a ruling.

---

## 9. Open questions for the owner

Each is a real fork with evidence on both sides. None is a
recommendation in disguise, and where this study has a preference it
says so and says why.

**Q1. Snapshot or sequential state visibility inside a frame?**

- *Snapshot (§4.1).* Matching becomes a pure function of the frame's
  start state, so FILTER and JOIN keep their algebraic properties, MERGE
  commutes, and termination is a one-line argument. Cost: an effect
  cannot see another effect's write in the same frame, so a chain
  advances one link per frame.
- *Sequential.* Chains can collapse inside one frame. Cost: the outcome
  depends on the order occurrences were drained, so every author must
  reason about queue order, and the JOIN's order-independence is lost.
- Evidence: GEDANKEN-7 shows the burning-hand chain already pays a
  frame per link at three of its seven joints, because each needs a
  physics step. The snapshot's cost is therefore mostly already paid.
  *This study's preference is snapshot, and it is the only preference
  here that changes if the owner disagrees, because §3 and §4 are built
  on it.*

**Q2. When two occurrences in one frame both claim a body's authority,
who wins?**

- *Larger imparted velocity.* Physical, mass-uniform, INV-10-clean:
  the strike that would move the body more takes it. Cost: it needs the
  honest price from Q7 to be meaningful.
- *Declared occurrence order.* Lexical, trivially deterministic, and
  arbitrary: a body shot and shoved in one frame ragdolls according to
  which queue was drained first.
- *Refuse both and report.* Fail closed. Cost: the body does nothing on
  the frame where two things happened to it, which is the frame it most
  needs to react.
- Evidence: none in the tree. Nothing today changes authority from an
  interaction at all.

**Q3. Is breakage-by-impact a missing physics law, or an authorable
effect?**

- *Physics.* Extend the tear law with an energy or peak-force
  criterion, so a bullet breaks a nail because the physics says so.
  Cost: the 12-frame rule exists for a reason (impulse-derived force is
  dominated by the damper under sustained rub, and it tore every grass
  bond, `physics_system_v4.cpp:4400-4406`) **[E]**, so a second
  criterion must not resurrect that.
- *Effect.* A `bond.break` STRUCTURE effect on a route, paid out of the
  occurrence's budget through WRAP. Cost: a game rule decides a
  structural physical outcome, and two mechanisms then answer "does
  this bond survive".
- Evidence: F2. Today neither exists, so a projectile cannot break
  anything in this engine.

**Q4. Do effects and conditions declare UCUM units?**

- *Yes.* INV-10's spirit becomes mechanical: a condition in impulse
  units cannot register, and a per-frame effect cannot be written
  without a time in its denominator (GEDANKEN-10). The machinery exists
  and is proven, `schema/physics.yaml` with 68 unit-annotated
  constants. **[E]**
- *No, cardinality only.* Cheaper: the timestep bug is caught, the
  cross-mass bug is not.
- Evidence: the cross-mass bug has already cost this project a campaign
  (INV-10's origin, the 1046:1 ratio ladder). The timestep bug has not
  happened yet only because `ON_CONTACT` fires once per episode
  (`particle_interaction_system.cpp:309-312`) **[E]** and becomes
  reachable the moment `CONTACT_SUSTAIN` ships.

**Q5. Does the engine ship a declared interaction session, so `hit` can
mean something?**

- *Yes.* A game opens and closes a session (`register_attack` /
  `end_attack` in logomancers' shape), and `hit` becomes `impact`
  inside a session. It is the only reason logomancers could tell a
  punch from a stumble.
- *No.* `hit` stays a pure game name in the `outcome` slot and the
  engine never learns it. Cost: every game that needs the distinction
  reimplements sessions.
- Evidence: no physical signal distinguishes intent. This is router Q6
  narrowed to the one thing that actually forces it.

**Q6. May an interceptor change the effect list, or only the name?**

- *Name only (as designed).* An interceptor changes meaning; effects
  act. Keeps the single-assignment theorem intact, because the effect
  list still comes from exactly one CLAIM route.
- *Name and effects.* A combat system with private frame state could
  substitute consequences directly. Cost: the theorem in §4.3 fails,
  because an interceptor could inject a second MOMENTUM effect, and
  every load-time guarantee becomes a runtime check.
- Evidence: the veto path already covers the "these consequences are
  wrong" case, provided a lower-priority route exists to fall through
  to. Whether authors will reliably write that fallback route is the
  real question.

**Q7. Does EXTERNAL authority get priced like the sleep cache?**

The largest finding, and it belongs on the physics board rather than in
the router's slice list.

- *Yes.* An EXTERNAL body is priced at its TRUE mass, the solver
  computes the interaction honestly, the half the writer will not take
  is refused and booked, and the writer decides. GEDANKEN-4's trunk
  then receives 0.389 m/s instead of 1.5. This is INV-31's argument
  applied to the other cache, and `physics_system_v4.cpp:515-526`
  already makes that argument in prose for sleep. **[E]**
- *No, keep the guess.* EXTERNAL stays infinite-mass. Cost, measured:
  every driven body over-delivers to whatever it touches by the ratio
  of the pair masses, which for a 70 kg walker into a 200 kg trunk is
  3.9x, in one frame. That over-delivery is the mechanism that tore
  canopies off trees.
- Evidence for the cost of doing it: the flip changes the effective
  mass of every row involving a driven body, so every humanoid contact
  measurement in the tree moves. It would need the same lever
  discipline `WAKE_RESOLVER` got (`:527-531`). **[E]**
- The router cannot compensate for either choice. It can only name what
  happened. That is worth stating plainly: **the effect algebra is
  downstream of this ruling and does not depend on which way it goes.**

**Q8. Is specificity-ordered selection compatible with the
rule-language ruling, or does the ruling get amended?**

`RULE_LANGUAGE.md:945-947` excludes "automatic firing, retraction,
conflict sets, and mutation ordering". **[E]** §4.6 argues Kamaji sits
outside all four, because it fires on pushed occurrences rather than on
graph state and because the snapshot rule makes an implicit loop
impossible. Three ways to close it:

- *Accept the argument as written.* Specificity is a selection rule
  over an externally supplied fact, not a firing rule over the graph.
  Cost: a later reader has to reconstruct the argument.
- *Amend the ruling* to say explicitly what the pushed family may do,
  so the boundary is in the document rather than in this study. Cost:
  editing a ruling, which needs care.
- *Reject ordering entirely.* Only one route may ever match a given
  (occurrence, side); a second is a load error. JOIN then disappears
  from the algebra and §3.2 is deleted. This is the purest option and
  it should not be dismissed: it makes the language smaller. Cost, and
  it is real: the silver-bullet pattern requires the general bullet
  route to explicitly exclude silver, so route tables stop being
  monotonically extensible and a meta-agent adding one route can
  invalidate the table it was added to.

---

## 10. What this document does not do

- It does not design the router. Occurrences, addresses, route schema,
  interceptors and slicing are `INTERACTION_ROUTER_DESIGN.md`.
- It does not answer motion authority. GEDANKEN-4 and GEDANKEN-5 show
  the algebra's dependency on it and stop there.
- It does not ship a combat model, a damage type, a material matrix or
  a threshold. Every number in §5.4 is a share or a declared unit; the
  one place a policy threshold would live is Q2 and it is open.
- It does not invent a derivation for "when does a struck body
  ragdoll". GEDANKEN-5's notes state what such a derivation would have
  to be a function of, and that none of the four inputs is exported
  today.
