# Why the sanitizer lane is clang and libc++, and not just "add ASan"

A memory-error detector is not one tool. It is a **compiler** and a
**standard library** cooperating, and the naive version of this lane
had the compiler without the library, passed in sixteen minutes, and
would have caught neither of the two real bugs it was built for.

This document exists because that failure is invisible: a lane that
detects nothing and a lane that has nothing to detect are the same
colour.

## The two bugs, and what makes them hard

**Bug one** shipped as issue #132 and broke Windows only. Simplified:

```cpp
const Choice* picked = &choices[1];   // a pointer INTO the vector
choices.clear();                      // the Choice it points at is destroyed
printf("%s", picked->key.c_str());    // read of dead memory
```

**Bug two** was found by this lane on its first run, in a test fixture:

```cpp
MutationPlaybackRegistry reg;   // declared FIRST  -> destroyed LAST
int started = 0, destroyed = 0; // declared second -> destroyed FIRST
```

C++ destroys locals in reverse order of declaration, so the counters die
before the registry that owns objects pointing at them, and
`~CountingPlay` writes `++(*destroyed_ptr)` through dead stack.

Measured on this machine, compiled normally, both run clean:

```
$ ./bug1_plain ; echo exit=$?
picked=2
exit=0
$ ./bug2_plain ; echo exit=$?
built
exit=0
```

Bug one prints the RIGHT answer. That is the whole problem. The memory
was freed, nobody else had claimed it yet, so the old bytes were still
sitting there and the read succeeded. On Windows the standard library
happened to zero that buffer on destruction, the read returned `""`,
and every drafted character silently became a Drifter.

**Undefined behaviour is not a crash. It is a coin flip weighted by
whatever the platform happens to do**, which is why it presented as a
Windows bug and was a bug everywhere.

## What a sanitizer adds

AddressSanitizer instruments the program so every read and write first
consults a side table describing which bytes are currently legal to
touch. Same machine, same two programs:

```
$ clang++ -fsanitize=address -fsanitize-address-use-after-scope bug1.cpp
$ ./b1_asan
ERROR: AddressSanitizer: container-overflow ... READ of size 1
    #0 in main bug1.cpp:12

$ ./b2_asan
ERROR: AddressSanitizer: stack-use-after-scope ... READ of size 4
    #0 in main bug2.cpp:16
```

Both caught, at the exact line, in a program that was silently "fine"
a moment ago.

## Why the standard library is half the tool

Here is the part that is not obvious, and the reason "add ASan" is not
a complete instruction.

The compiler knows about memory the COMPILER controls: stack frames it
lays out, allocations it can see. It has no idea what a `std::vector`
means. A vector holds a buffer that is usually **larger than the number
of live elements in it** (that is what `capacity()` versus `size()` is).
After `choices.clear()`, the buffer is still allocated and still legally
addressable as far as the allocator is concerned. Nothing the compiler
knows says those bytes stopped being live objects.

The only component that knows is the vector itself. So libc++ tells the
sanitizer, from inside its own header:

```
libc++ .../__vector/vector.h:684
  std::__annotate_contiguous_container<_Allocator>(
      data(), data() + capacity(), __old_mid, __new_mid);
```

That call is the vector announcing "the live region used to end here,
it now ends there". Without it the sanitizer has a buffer and no idea
which part of it is real, and a read into the dead part looks like a
perfectly ordinary read of allocated memory.

This is why the error is named `container-overflow` rather than
use-after-free. It is a diagnosis only the container could have made.

**A standard library that does not emit those annotations cannot report
this class of bug, no matter which compiler you use.**

## The measurement that decided the lane

Measured against both real bugs (the toolchain rows were measured in a
clean `ubuntu:24.04` container; the clang+libc++ row is reproduced on
the macOS dev machine, which uses clang and libc++ natively):

| toolchain | #132 container-overflow | use-after-scope |
|---|---|---|
| gcc + libstdc++ | miss | miss |
| clang + libstdc++ | miss | **catch** |
| clang + libc++ | **catch** | **catch** |

Two independent gaps, which is why only the bottom row is acceptable:

- **The library gap.** libstdc++ as shipped does not emit the container
  annotations, so the container-overflow column is blank for both
  libstdc++ rows regardless of compiler.
- **The compiler gap.** gcc missed use-after-scope at `-O0`, `-O1` and
  `-O2`, including with `-fsanitize-address-use-after-scope` passed
  explicitly.

The runner's default toolchain is gcc with libstdc++, which is the top
row. That is the lane that passed in sixteen minutes and proved nothing.

## Consequences that follow, and are not incidental

**Cost is 5.5x to 7.6x, not the usual 2x.** Build 10m07s, ctest 38s,
twelve lives 3m40s, job total 14m49s. The build step needs a 15-minute
cap: at the old 10-minute default it would have been killed, and a
timeout kill is reported identically to a real finding.

**`test_chargen` is excluded and the exclusion is named in the
workflow.** 565s sanitized against 74s plain. It is covered instead by
twelve sanitized lives walking the same `chargen.cpp`, which between
them reach Draft three times and Drifter four, both sides of the fork
the original bug corrupted. A silent exclusion would read as coverage.

**The lane is advisory.** A finding is loud and blocks nobody.

## The known noise, and why it is not suppressed

Nine failures on landing, unchanged across four runs (jobs 95769694816,
96317436155, 96331614800, 96465620076 — same nine names every time).
**One is real**: the fixture bug above, not fixed and not suppressed.

**Eight are the instrument**, and the earlier version of this section
said all eight print `alloc-dealloc-mismatch`. Read off the runs, seven
do:

| test | throw that raises it |
|---|---|
| `test_kg_ops_parse` | `kg_ops_parse.cpp:217`, caught `:221` |
| `test_ontology_extension` | `test_ontology_extension.cpp:193`, caught `:201` |
| `test_ontology_validator` | `ontology_validator.cpp:217` |
| `test_outcome_executor` | same, via `KGModule::setProperty` |
| `test_kg_parse_safety` | `finite_float.cpp:76`, the `std::stod` refusal |
| `test_rule_fork` | same, via `validate_create` |
| `test_logotron_director` | `director_parser.cpp:32`, caught `:36` |

Every frame between the `new` and the `free` is inside prebuilt
libc++/libc++abi. Reproduced with a six-line program that only throws
and catches a `std::runtime_error`, with zero allocation of our own.
Ubuntu's libc++ is not itself built with ASan, so `new` is intercepted
inside our instrumented binary while the matching `free` happens inside
the uninstrumented library, and the sanitizer sees a mismatched pair
that is entirely an artifact of the packaging.

**The eighth is not a sanitizer report at all**, and calling it one hid
what it is. `test_kg_property_gate` exits 1 with two ordinary assertion
failures out of fourteen checks: `wrong-typed value write aborts under
strict gate` and `message states the expected value type`. Those two
are the only checks in the file whose fixture needs a *throw*:
`write_aborts()` forks and requires the child to die by `SIGABRT`, and
the child runs `std::stod("banana")` through `parse_finite_binary64`.
Under ASan that throw raises the packaging artifact, ASan reports and
exits without an abort, and the death test sees no abort. The gate is
fine — its two red paths that reach `abort()` without throwing both
pass here. The cause is inferred rather than read: the child's stderr
is captured into a string the test only greps and never prints.

One line of `ASAN_OPTIONS=alloc_dealloc_mismatch=0` silences the seven.
It also gives up every genuine new/free mismatch in our own code, which
is a real detector traded for a quiet log. Left as an open decision
rather than taken quietly.

## Red means new

Nine is not a number anyone reads. A red job is a red job, so a *tenth*
failure — the entire reason this lane exists — was indistinguishable
from the nine it already had.

Every one of the nine now has a row in
`tests/invariants/SANITIZER_AUDIT.jsonl`, carrying `TEST_AUDIT.jsonl`'s
shape plus one field, `finding`, which is what separates "an instrument
reported this" from "a test failed". `scripts/check-sanitizer-audit.py`
runs at the end of the lane and compares the failing set against the
audited set in **both** directions:

- a test failing with no audited row is a new memory error, and fails
  the job;
- an audited row that stops reporting fails the job too, because a red
  that silently goes green is as much a change as a new one. That is
  the same direction-locking `physics-linux` applies to its born-red
  ladders.

One thing that is not obvious and cost a case: CTest writes
`LastTestsFailed.log` **only when something failed**, so an absent log
means either that every test passed or that the step died before
finishing. The step records ctest's exit code beside the log, and the
checker reads both.

`scripts/test-sanitizer-audit.sh` proves the gate on eight cases and runs
on every pull request, in `merge-policy`, because the lane it guards
does not.

Why a second file rather than rows in `TEST_AUDIT.jsonl`: that file is
keyed on the test name with one row each, and its `expect` field is
lane-agnostic. Six of these nine already have a row there saying
`expect: pass`, which is true in `headless-linux` and false here.

## The one-sentence version

A sanitizer is a compiler and a standard library cooperating; we run
clang with libc++ because the compiler alone cannot see inside a
`std::vector`, and the bug that started this was inside a
`std::vector`.
