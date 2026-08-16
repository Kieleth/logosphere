# Lessons owed upstream to malleus

Findings this project paid for that are generic enough to belong in the
Ordo Malleus rubric. Shaped as rubric entries so they lift straight into
`malleus-dev/src/malleus/inquisition/rubric.yaml`.

`MALLEUS_INQUISITION.md` holds findings ABOUT this repo, written by the
inquisition rite and owned by it. This file holds lessons FOR malleus.
Different direction, different owner, so they stay apart.

Everything under "For upstream" is written without project specifics and
can be sent as-is. The "Where we hit it" sections stay here.

---

## 1. A reader census counts types; the hole is per-instance

### For upstream

Refines the existing `reader_census` rite, which it would not have caught.

```yaml
  - id: instance_reader_coverage
    question: >-
      Is every populated region of the graph reached at runtime, or does a
      type-level reader census pass while whole subgraphs stay unread?
    severity: SUSPICION
    lesson: >-
      A reader census is satisfied by one reader touching one instance. A
      type can be read on Monday, extended with a hundred new instances on
      Tuesday, and the census still reports it healthy while none of the
      hundred is ever reached. Observed: a table of two dozen rows, every
      row cited to its source, every count asserted, referenced by exactly
      one thing in the repository, which was the generator that wrote it.
      A neighbouring path reached the same content through a side door, so
      the type had a reader and the census was clean. The escalation
      condition is whether the engine EXECUTES the graph: where unread data
      means only wasted storage this is a SUSPICION, and where the graph is
      the program it is a HERESY, because unread rules are silently absent
      behaviour rather than dead weight.
```

Two mechanical checks discharge it, and they catch different amounts:

- **Static, cheap.** Root at the graph's entry points, walk references,
  report populated types with unreachable instances. Catches whole dead
  regions. Blind to anything a side door reaches, which in our case was
  seventeen of twenty-three.
- **Dynamic, complete.** Record which entity ids the runtime actually
  acted on, sweep, and require every instance of the executed types to be
  reached at least once or to carry a declared exception. Catches all of
  them, costs a journal and a sweep.

### Where we hit it

Twenty-three of twenty-four careers print a rank 0 grant. All twenty-three
were absorbed, cited to the cell they came from, verified against the
source text, counted by an invariant, and read by nothing: the promotion
rule consults the rank ladder only from rank 1 up. Every generated
character finished a skill level short of the book for as long as the
feature had existed. A model auditing OUTPUT found it. No gate did.

---

## 2. Green fidelity gates read as proof of consumption, and are not

### For upstream

```yaml
  - id: claim_kinds_distinguished
    question: >-
      Do the project's gates distinguish fidelity, coherence and
      consequence, or does a wall of green on the first two read as
      assurance about the third?
    severity: NOTE
    lesson: >-
      Three independent claims get conflated because they are all "the
      checks pass". FIDELITY is that the data matches its source: citations
      resolve, values appear in the text they quote. COHERENCE is that the
      assembled graph is well-formed: no dangling references, ranges tile,
      ordered parts are contiguous. CONSEQUENCE is that the system acts on
      it. A project can hold the first two to an unusually high standard,
      as this one does, and have no vocabulary at all for the third, and
      the strength of the first two is precisely what makes the absence
      invisible. Name the three in the docs, and state which gate serves
      which, so a reader cannot mistake a fidelity gate for a consequence
      gate.
```

### Where we hit it

The seed verifier proves every citation resolves in the vendored source
and every number appears in the sentence it quotes. It is a genuinely
strong instrument and it has caught real defects, including four in the
published book. It says nothing whatsoever about whether the game reads
the data, and the confidence it earns is exactly what let a dead table
survive review.

---

## 3. `unmodelled` needs a sibling for the unread case

### For upstream

The existing `reader_census` lesson already reaches for this: "Write-only
types need an explicit write-only-with-reason annotation or a reader." The
mirror is missing and the two are different failures.

```yaml
  - id: unread_declared
    question: >-
      Can a record say "nothing reaches me yet, and here is why", or is
      silence the only way to express it?
    severity: NOTE
    lesson: >-
      Partial absorption already has a home in most adopting schemas: a
      slot recording the part of a rule the graph does not yet express, so
      a partial rule cannot look whole. That slot describes a record whose
      EXPRESSION is incomplete. It has no answer for a record that is
      fully expressed and never invoked, which is the failure a coverage
      gate surfaces. Without a declared-exception slot the gate has only
      two settings, pass and nuisance, and the second one gets it switched
      off. With one, the gap is a countable number of argued exceptions
      instead of a silence.
```

---

## 4. The skill's resolution order degrades upward, silently

### For upstream

The skills open with the installed package as source of truth:

```
python -c "from malleus.ontology import bundled_ontology_path; print(...)"
```

On this machine that raises `ImportError: cannot import name
'bundled_ontology_path'`. The command is correct and the docs are fine.
The installed package is **0.1.0**; the checkout beside it is **0.6.0**,
where the helper exists (`src/malleus/ontology.py:23`, exported from
`__init__.py`). Five minor versions of drift, surfacing as a confusing
import error from the bootstrap step rather than as a version complaint.

The lesson is the failure MODE, not the missing symbol:

```yaml
  - id: guidance_newer_than_runtime
    question: >-
      When the documented resolution order falls through from the installed
      package to a local checkout, does anything notice that the adopter is
      now reading guidance newer than the code they run?
    severity: SUSPICION
    lesson: >-
      A resolution order that tries the installed package first and a
      developer checkout second degrades in the dangerous direction. The
      fallback is not equivalent, it is NEWER: the adopter reads the newest
      rubric, rites and protocol docs while their runtime executes a
      package several minor versions old. Observed: a bootstrap command
      from the current skill raising ImportError against an installed
      0.1.0 while a 0.6.0 checkout sat beside it, the symbol having been
      added in between. This is root_currency inverted, and worse, because
      root_currency compares two copies of the root and this compares
      documentation against code, which nothing diffs. Make the version a
      first-class output of the bootstrap step: print the installed
      version, print the checkout version, and refuse when they disagree,
      instead of letting an ImportError stand in for "your package is old".
```

Concretely: have `malleus-inquisitor` print the resolved bundled path AND
the installed version, so one command answers "which root am I actually
running against", and a stale install announces itself.

---

## 5. An unknown range blinds seven unrelated rites

### For upstream

```yaml
  - id: unknown_range_blast_radius
    question: >-
      When a schema names a range the loader does not recognise, does
      the failure stay on the SLOT, or does it take the whole file and
      everything importing it?
    severity: HERESY
    lesson: >-
      An unknown range fails construction, construction is rite one, and
      a failed rite one short-circuits the run. So one unrecognised name
      does not reject one slot: it silently blinds every later rite on
      that file and on every file importing it. Observed: an adopter
      used a range that is a legal built-in of the schema language and
      simply absent from the loader's allowlist; five files of thirteen
      failed to construct, and SEVEN of eight mechanical rites had
      therefore never executed anywhere in that repository. Nobody knew,
      because the report showed one heresy per file and said nothing
      about what it had skipped. Two fixes, independent: widen the
      allowlist to the schema language's full set of built-ins, since
      rejecting a legal type punishes an adopter for using their tools
      correctly; and make construction failure REPORT the rites it
      prevented from running, so the blast radius is visible rather
      than inferred.
```

The allowlist that produced this is five names where the schema
language defines nineteen. The missing fourteen include the
double-precision float, the decimal, the date and the URI, which is to
say the ones an adopter reaching for precision will use first.

### Where we hit it

Two slots declared a double-precision float, correctly, because the
engine stores those bounds as a C++ `double`. Writing the narrower type
to satisfy the loader would have made the schema describe an engine we
do not have, so we declared a named type that resolves down to the
loader's nearest builtin and kept the meaning in its description. The
fix took ten minutes. Finding it took a day, and only because a rite we
could not run was the thing we were trying to run.

### Also, a false-finding generator

The inquisitor CLI takes `--map` for import resolution. Invoked without
maps on a schema that imports a sibling, it reports "does not
construct", which is indistinguishable in the output from a real
construction failure. Twelve of our schemas appeared broken this way
and none of them were. Either resolve sibling imports relative to the
inspected file by default, or make an unresolved import say
`CANNOT INSPECT` rather than `HERESY`. As it stands the tool
manufactures findings against correct schemas, and an adopter's first
run is exactly when they cannot tell the difference.

---

## 6. An ontology that cannot hold a half-known concept forces a lie

### For upstream

This is a FEATURE request with a rite attached, not only a rite. The
rite alone would report a gap malleus currently gives adopters no way
to close.

**The pain point.** Ingesting a body of published rules, a term is
routinely used before the document defines it. Chapter 9 says
"characters receive one benefit per term served" and *benefit* is
defined in chapter 3, or chapter 14, or by example only. A reader
processing chapter 9 has three moves available and all three are bad:
invent a definition (a rule that is subtly wrong while reading
perfectly), refuse the passage (which strands most of the document,
because forward reference is normal prose, not an error), or write a
string and resolve it later (which is the magic-strings heresy under a
different name).

The missing capability is a **provisional concept**: an entity that
exists, is addressable, can be referenced and reinforced by later
passages, and is typed so that nothing can read it as settled. Lewis's
rule of accommodation is the formal ancestor: a presupposition required
by what is said comes into existence, *provided nobody objects*. The
objection clause is the part a knowledge system has to supply, and it
is exactly what an ontology is for.

Note this is close to, but not the same as, the staging overlay.
Staging asks "has this claim been assented to". Accommodation asks "is
this concept fully known yet". A staged claim about a settled concept
and a committed claim about a half-known concept are different states
and an adopter needs both.

**The trap to specify against.** Reinforcement (counting later
references) is valuable for ranking what to resolve first, and it must
never promote. A concept referenced forty times and never defined is
the most important open question in the ingest; it is not thereby
defined. Any implementation where accumulated weight settles a concept
has built a half-open gate on its own most load-bearing entities.

```yaml
  - id: provisional_concept_status
    question: >-
      When the source uses a concept before defining it, can the graph
      hold that concept in a typed provisional state, and is that state
      impossible to mistake for a settled one at read time?
    severity: HERESY
    lesson: >-
      Documents introduce terms before defining them; this is normal
      prose, not a defect in the source. A knowledge system with no
      provisional state offers its writers three moves and all three
      are heresies: invent the definition, refuse the passage, or store
      an unresolved string and promise to fix it later. The third is the
      one that gets chosen, because it is the only one that lets the
      ingest continue, and it reintroduces magic strings under a name
      nobody is watching. Require a distinct provisional status that is
      addressable and referenceable, that names the source location
      forcing it, that no executable path may consume, and that is
      cleared only by a definition being found and judged. Reference
      counting may rank the backlog and must never settle a concept:
      promotion by accumulated weight rebuilds the half-open gate on
      precisely the entities the graph leans on hardest. A system with
      no such state will report a clean rejection rate while its writers
      route around it, which is indistinguishable from a healthy one.
```

### Where we hit it

Reading a published RPG rulebook into an executable graph. Full design
and the prior art it rests on: `docs/CUMULATIVE_INGESTION.md` (R11 and
R12 in `docs/REFLECTION_PROTOCOL.md`). We found it from the other end:
our seed verifier takes the prior world as an optional argument, and
the owner's ruling was that tightening that signature treats a
representation gap as a call-signature defect. The caller's choice
between a lie and a refusal exists whatever the signature says, because
the ontology has nowhere to put a half-known concept.

---

## Observation, not a lesson

`MALLEUS_INQUISITION.md` records "rubric v1"; the rubric on this machine
is `version: 4`. If that survey was run against v1, rites added since have
never been applied here. Owned by whoever runs the rite, noted only
because it bears on which findings this repo can claim to have cleared.
