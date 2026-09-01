#!/usr/bin/env python3
"""Generate lives and have a model say which of them cannot legally exist.

WHY THIS AND NOT MORE TESTS. A test checks what someone already thought
of. This sweeps lives nobody wrote by hand and asks whether the book
allows them, which is the only way to find a rule that is wrong in a
direction we never considered. The 241-abort class found in the August
audit was exactly that shape: nothing was failing, the sweeps just
quietly reported the survivors.

WHY IT IS AFFORDABLE. Every life is a SEED. A finding arrives as a
number, and

    ./build/logovger-headless --random <seed>

replays that exact life forever, for nothing, without calling a model
again. That is what record/replay was built for and this is where it
pays: verification costs a rerun instead of an argument.

FINDINGS ARE CANDIDATES, NOT VERDICTS. A model reading a rulebook is
wrong sometimes, in both directions. The August audit was right about
the aborts and wrong about medical debt, and only checking each claim
by hand separated them. So this writes findings out with their seed and
acts on none of them: triage is a human reading the replay. What
survives triage becomes a deterministic test, which is where a finding
should end its life.

TWO PASSES.
  checklist - every life, against the rules we believe we implemented.
              Cheap, precise, and can only catch regressions.
  chapter   - a sample, against the SRD text itself. Open-ended, so it
              can name a rule we never encoded. Noisier, and that is
              the price of the only pass that can surprise us.

Needs ANTHROPIC_API_KEY. No key is an error, never a skip: an audit
that quietly does not run reads exactly like one that passed.

Usage:
    python3 examples/logovger/tools/audit_lives.py \\
        ./build/logovger-headless 1 40 \\
        examples/logovger/audits/lives.json [--sample 6]
"""
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

MODEL = "claude-haiku-4-5-20251001"
API = "https://api.anthropic.com/v1/messages"

# Lives per call. Small batches keep one bad life from poisoning the
# judgement of the others and keep each response short enough to stay
# structured.
BATCH = 4

HERE = os.path.dirname(os.path.abspath(__file__))
# The corpus is vendored at the repository root, outside every
# game (cmake/corpora.cmake).
CHAPTER = os.path.join(HERE, "..", "..", "..", "corpora",
                       "cepheus-srd", "book1",
                       "character-creation.md")

# What we believe chapter 1 does. Deliberately written as CLAIMS, so a
# violation is a disagreement between this list and the generated life,
# and either one of them can be the thing that is wrong.
CHECKLIST = """\
 1. Six characteristics, each a 2D6 result, in order Str Dex End Int Edu Soc.
 2. (Ages are NOT audited here. Measured over two runs, every age
    finding was the model's arithmetic and not the generator's: it
    double-counted mishap years, counted terms that ended in death,
    and recomputed 18 + 4 x terms while saying the total matched.
    Arithmetic over a printed timeline is what a deterministic test
    does perfectly and for nothing, so it stays there. Ask a model
    what it is good at.)
 3. Qualification is one throw; failing it means the Draft or Drifter.
 4. A DM of -2 applies to qualification for each PREVIOUS career entered.
 5. A first career grants every skill on its service table at level 0;
    a later career grants exactly one of them.
 6. Survival is thrown every term. A natural 2 always fails, whatever
    the DM makes the total.
 7. A failed survival is death, unless the mishap table is taken instead.
 8. Every mishap ends the career and costs two years, except the prison
    row, which costs four and no more.
 9. Commission is available at rank 0 only; advancement at rank 1 or
    higher. Either grants one extra training roll.
10. A career with neither commission nor advancement grants two
    training rolls a term instead of one.
11. Advanced Education may only be rolled on at Education 8 or higher.
12. Aging begins at age 34, and is 2D6 minus TOTAL terms served.
13. A characteristic reduced to 0 is a crisis: pay 1D6x10,000 or die.
    The life states which happened - "bought back from the" means paid,
    "care unpaid" means refused. A quoted price is not a payment.
    Paying restores it to exactly 1 and bars all future qualification.
14. Re-enlistment is a throw. A natural 12 forces another term and
    outranks the seven-term cap.
15. Benefits are one roll per term served in this career, plus 1 at
    rank O4, 2 at O5, 3 at O6. At most three may be taken as cash.
"""

CHECKLIST_PROMPT = """\
You are auditing a computer-generated Traveller-style character.

Below are claims about the rules, then %d generated lives. For EACH
life, say whether it violates any claim.

Judge only what the life SHOWS. Every roll is printed with its dice,
its modifier, its target and its result. Do not assume a rule was
applied off-screen, and do not object to a value merely because it is
unusual - improbable is not illegal.

You cannot see the career tables, so you do not know which skills a
career's service table holds. Never claim a skill came from the wrong
table. Judge the STRUCTURE of what happened, not the contents of
tables you were not given.

RULES WE BELIEVE WE IMPLEMENTED:
%s

LIVES:
%s

Return ONLY a JSON array, one object per life that violates something:
  [{"seed": <int>, "claim": <int>, "what": "<what the life shows>",
    "why": "<why that breaks the claim>"}]
Return [] if every life is legal. Do not explain outside the JSON.
"""

CHAPTER_PROMPT = """\
You are auditing computer-generated Traveller-style characters against
the rulebook chapter that produced them.

Read the chapter, then the %d generated lives. For EACH life, name any
sentence of the chapter the life contradicts.

Judge only what the life SHOWS: every roll is printed with its dice,
modifier, target and result. Improbable is not illegal. Prefer saying
nothing to guessing.

You are looking especially for rules the generator seems NOT TO KNOW
AT ALL - something the chapter requires that never appears in any life.

CHAPTER:
%s

LIVES:
%s

Return ONLY a JSON array:
  [{"seed": <int>, "quote": "<the sentence, verbatim>",
    "what": "<what the life shows>", "why": "<how it contradicts>"}]
Return [] if you find nothing. Do not explain outside the JSON.
"""


def live(binary, seed):
    """One generated life, as the timeline a reader would see."""
    done = subprocess.run([binary, "--random", str(seed)],
                          capture_output=True, text=True, timeout=120)
    lines = [l for l in done.stdout.split("\n")
             if not l.startswith("[KG]") and not l.startswith("[telemetry]")]
    return "\n".join(lines).strip()


def ask(prompt, key):
    body = json.dumps({
        "model": MODEL, "max_tokens": 4000,
        "messages": [{"role": "user", "content": prompt}],
    }).encode()
    request = urllib.request.Request(
        API, data=body,
        headers={"x-api-key": key, "anthropic-version": "2023-06-01",
                 "content-type": "application/json"})
    with urllib.request.urlopen(request, timeout=180) as response:
        payload = json.load(response)
    text = payload["content"][0]["text"].strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1].rsplit("```", 1)[0]
    return json.loads(text)


def batched(lives, size):
    for at in range(0, len(lives), size):
        yield lives[at:at + size]


def render(batch):
    return "\n\n".join("=== life from seed %d ===\n%s" % (seed, text)
                       for seed, text in batch)


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    binary, first, last, out_path = (sys.argv[1], int(sys.argv[2]),
                                     int(sys.argv[3]), sys.argv[4])
    sample = 0
    if "--sample" in sys.argv:
        sample = int(sys.argv[sys.argv.index("--sample") + 1])

    key = os.environ.get("ANTHROPIC_API_KEY")
    if not key:
        print("REFUSED: ANTHROPIC_API_KEY is not set. This audit is the "
              "only thing looking for rules nobody thought to test, and "
              "one that silently does not run reads like one that passed.")
        return 1

    lives = []
    for seed in range(first, last + 1):
        text = live(binary, seed)
        if not text:
            print("  seed %d produced no life" % seed)
            continue
        lives.append((seed, text))
    print("generated %d lives from seeds %d..%d"
          % (len(lives), first, last))
    if not lives:
        print("REFUSED: no lives to audit")
        return 1

    findings = {"checklist": [], "chapter": []}

    for batch in batched(lives, BATCH):
        prompt = CHECKLIST_PROMPT % (len(batch), CHECKLIST, render(batch))
        try:
            found = ask(prompt, key)
        except urllib.error.HTTPError as error:
            print("REFUSED: the model call failed: %s" % error.read()[:300])
            return 1
        findings["checklist"].extend(found)
        print("  checklist %s: %d finding(s)"
              % ([s for s, _ in batch], len(found)))

    if sample:
        chapter = open(CHAPTER, encoding="utf-8").read()
        for batch in batched(lives[:sample], BATCH):
            prompt = CHAPTER_PROMPT % (len(batch), chapter, render(batch))
            try:
                found = ask(prompt, key)
            except urllib.error.HTTPError as error:
                print("REFUSED: the model call failed: %s"
                      % error.read()[:300])
                return 1
            findings["chapter"].extend(found)
            print("  chapter %s: %d finding(s)"
                  % ([s for s, _ in batch], len(found)))

    # Written with the seed on every finding, because that is what makes
    # one checkable. Nothing here is acted on until a human replays it.
    report = {
        "generated": len(lives),
        "seeds": [seed for seed, _ in lives],
        "how_to_check": "./build/logovger-headless --random <seed>",
        "findings": findings,
    }
    json.dump(report, open(out_path, "w", encoding="utf-8"), indent=1)
    open(out_path, "a", encoding="utf-8").write("\n")
    total = len(findings["checklist"]) + len(findings["chapter"])
    print("%d candidate finding(s) -> %s" % (total, out_path))
    print("None of them is a verdict. Replay each seed before believing it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
