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

## Observation, not a lesson

`MALLEUS_INQUISITION.md` records "rubric v1"; the rubric on this machine
is `version: 4`. If that survey was run against v1, rites added since have
never been applied here. Owned by whoever runs the rite, noted only
because it bears on which findings this repo can claim to have cleared.
