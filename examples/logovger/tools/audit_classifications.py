#!/usr/bin/env python3
"""Have a model audit what the extractor decided each cell MEANS.

The split this enforces, and the reason it exists:

    TRANSCRIPTION IS MECHANICAL. A model that misreads a number
    produces a rule that is wrong while the prose still reads fine,
    and byte-exact citations only work if the bytes were copied
    rather than retyped. So the extractor does the reading.

    MEANING IS JUDGEMENT. Deciding that "Low Passage" is a thing you
    own, "+1 Int" is a characteristic and "20000" is money is
    interpretation, and interpretation checked only by the regexes
    that produced it is not checked at all.

So the extractor classifies, and a model independently classifies the
same values without seeing the extractor's answer. Disagreement fails
the run. Agreement is written to an audit file that a test compares
against the seed, so a value the audit has never seen turns the suite
red rather than shipping unexamined.

Needs ANTHROPIC_API_KEY. No key is an error, never a skip: an audit
that quietly does not run is worse than no audit, because it reads as
one that passed.

Usage:
    python3 examples/logovger/tools/audit_classifications.py \\
        examples/logovger/seeds/cepheus_book1_career_tables.json \\
        examples/logovger/seeds/classification_audit.json
"""
import json
import os
import sys
import urllib.error
import urllib.request

MODEL = "claude-haiku-4-5-20251001"
API = "https://api.anthropic.com/v1/messages"

# The tables whose cells hold ONE value with one meaning. Rank cells
# are excluded on purpose: "Lt Commander \\[Tactics-1\\]" is a title and
# a grant together, a different question that this audit would answer
# badly by forcing it into one category.
AUDITED_TABLES = {"Personal Development", "Specialist", "Adv Education",
                  "Cash Benefits", "Cost Benefits", "Material Benefits",
                  "Service Skills"}
BATCH = 40

# Disagreements a human has settled, with the reason, so the audit
# keeps failing on NEW ones. An unexplained override would make this
# tool decorative: the point is that a difference of reading reaches a
# person once, not that it can be waved away.
ADJUDICATED = {
    "0": ("MONEY",
          "The Barbarian's first Cash Benefits row prints 0. The audit "
          "reads zero money as nothing, which is fair, but the cell is a "
          "cash result whose amount is zero and a benefit roll was spent "
          "to get it. Recording it as no effect would lose that."),
}

# What the extractor's outcome types mean, in the words the model is
# asked to use. The model never sees these mappings.
BY_OUTCOME = {
    "AdvanceSkill": "SKILL",
    "ModifyAttribute": "CHARACTERISTIC",
    "GainFixedMoney": "MONEY",
    "GainPossession": "POSSESSION",
    "NoEffect": "NOTHING",
}

PROMPT = """You are auditing how cells from a science-fiction roleplaying \
rulebook's career tables have been interpreted. These come from tables \
titled Personal Development, Service Skills, Specialist, Adv Education, \
Cash Benefits and Material Benefits.

Classify each cell value as exactly one of:

  SKILL          a trained ability the character learns
  CHARACTERISTIC an increase to an innate attribute such as strength
  MONEY          a sum of currency
  POSSESSION     a physical thing, ticket, membership or share owned
  NOTHING        an empty cell, no result

Answer with a JSON object mapping each value to its classification and \
nothing else. No commentary, no code fences.

Values:
%s"""


def classify(values, key):
    body = json.dumps({
        "model": MODEL,
        "max_tokens": 4000,
        "messages": [{"role": "user",
                      "content": PROMPT % json.dumps(values, indent=1)}],
    }).encode()
    request = urllib.request.Request(
        API, data=body,
        headers={"x-api-key": key,
                 "anthropic-version": "2023-06-01",
                 "content-type": "application/json"})
    with urllib.request.urlopen(request, timeout=120) as response:
        payload = json.load(response)
    text = payload["content"][0]["text"].strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1].rsplit("```", 1)[0]
    return json.loads(text)


def main():
    seed_path, audit_path = sys.argv[1], sys.argv[2]
    seed = json.load(open(seed_path, encoding="utf-8"))

    # What the extractor decided, read back out of the seed itself
    # rather than from the extractor, so the audit checks what SHIPPED.
    ours = {}
    for op in seed["ops"]:
        if op.get("op") != "create_entity":
            continue
        kind = BY_OUTCOME.get(op["type"])
        if not kind:
            continue
        properties = op["properties"]
        quote = properties.get("source_quote", "")
        if not quote or properties.get("source_kind") != "cell":
            continue
        if properties.get("source_table") not in AUDITED_TABLES:
            continue
        if quote in ours and ours[quote] != kind:
            print(f"REFUSED: the extractor classified {quote!r} as both "
                  f"{ours[quote]} and {kind}")
            return 1
        ours[quote] = kind

    key = os.environ.get("ANTHROPIC_API_KEY")
    if not key:
        print("REFUSED: ANTHROPIC_API_KEY is not set. This audit is the "
              "only independent check on what the extractor decided each "
              "cell means; skipping it silently would leave the "
              "classification checked by nothing but the regexes that "
              "produced it.")
        return 2

    values = sorted(ours)
    theirs = {}
    for start in range(0, len(values), BATCH):
        batch = values[start:start + BATCH]
        try:
            theirs.update(classify(batch, key))
        except urllib.error.HTTPError as failure:
            print(f"REFUSED: the model could not be reached: {failure}")
            return 2
        except json.JSONDecodeError as failure:
            print(f"REFUSED: the audit did not return JSON: {failure}")
            return 2

    missing = [v for v in values if v not in theirs]
    if missing:
        print(f"REFUSED: the audit returned no verdict for {len(missing)} "
              f"value(s): {missing[:8]}")
        return 1

    disagreements = []
    settled = {}
    for value in values:
        if theirs[value] == ours[value]:
            continue
        ruling = ADJUDICATED.get(value)
        if ruling and ruling[0] == ours[value]:
            settled[value] = {"audit_said": theirs[value],
                              "kept": ours[value], "because": ruling[1]}
            continue
        disagreements.append((value, ours[value], theirs[value]))
    for value, mine, model in disagreements:
        print(f"  DISAGREEMENT {value!r}: extractor says {mine}, "
              f"audit says {model}")
    for value, ruling in settled.items():
        print(f"  settled earlier: {value!r} kept as {ruling['kept']} "
              f"though the audit reads it as {ruling['audit_said']}")

    audit = {
        "source": seed["source"],
        "model": MODEL,
        "checked": len(values),
        "agreed": len(values) - len(disagreements),
        "classifications": {v: ours[v] for v in values},
        "adjudicated": settled,
    }
    json.dump(audit, open(audit_path, "w"), indent=1, sort_keys=True)
    open(audit_path, "a").write("\n")

    print(f"audited {len(values)} distinct cell values against {MODEL}")
    print(f"agreed on {len(values) - len(disagreements)}")
    if disagreements:
        print(f"REFUSED: {len(disagreements)} disagreement(s). Either the "
              f"extractor is wrong or the source is stranger than it "
              f"looks; both are worth reading before this ships.")
        return 1
    print(f"wrote {audit_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
