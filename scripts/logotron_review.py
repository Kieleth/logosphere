#!/usr/bin/env python3
# Logotron session review — tiny CLI that makes AI feedback cheap.
#
# Usage:
#   scripts/logotron_review.py                # list sessions
#   scripts/logotron_review.py <session_dir>  # summarize every round
#   scripts/logotron_review.py <session_dir> <round_num>   # full tick dump
#
# The intent: after a session you want to tell the AI "this was
# dumb, go fix it". That conversation needs a shared reference.
# This script turns a round's JSONL into a terse narrative — how
# far the AI got, when it saw the opponent, what crashed it — so a
# human can read it in 10 seconds and annotate with feedback.

from __future__ import annotations
import argparse
import json
import os
import sys
from pathlib import Path

SESSIONS_ROOT = Path.home() / ".logosphere" / "sessions"


def list_sessions() -> None:
    if not SESSIONS_ROOT.exists():
        print(f"No sessions yet at {SESSIONS_ROOT}")
        return
    print(f"{'build':12}  {'launch':22}  path")
    for build in sorted(SESSIONS_ROOT.iterdir()):
        if not build.is_dir():
            continue
        for sess in sorted(build.iterdir()):
            manifest = sess / "session.json"
            if not manifest.exists():
                continue
            try:
                m = json.loads(manifest.read_text())
            except json.JSONDecodeError:
                continue
            launch = m.get("launch_utc", "?")
            print(f"{m.get('build_sha', '?'):12}  {launch:22}  {sess}")


def summarize_session(session_dir: Path) -> None:
    manifest = session_dir / "session.json"
    if not manifest.exists():
        print(f"No session.json in {session_dir}", file=sys.stderr)
        sys.exit(2)
    m = json.loads(manifest.read_text())
    print(f"Session {session_dir.name}")
    print(f"  build:    {m.get('build_describe', '?')}")
    print(f"  launched: {m.get('launch_utc', '?')}")
    print(f"  ended:    {m.get('end_utc', '?')}")
    print()

    logotron = session_dir / "logotron"
    if not logotron.exists():
        print("  (no logotron data)")
        return

    metas = sorted(logotron.glob("round_*_meta.json"))
    if not metas:
        print("  (no rounds recorded)")
        return
    print(f"  rounds: {len(metas)}")
    for meta_path in metas:
        try:
            r = json.loads(meta_path.read_text())
        except json.JSONDecodeError:
            continue
        narr = summarize_round(logotron, r)
        print(f"    #{r.get('round', '?'):>3}  {r.get('outcome', '?'):<10}  "
              f"{r.get('duration_s', 0):5.1f}s  "
              f"{r.get('decisions', 0):>3} decisions  "
              f"personality={r.get('personality', '?'):<10}  {narr}")
        # Show each recorded crash on its own line — this is where
        # the "why did I die" question gets answered at a glance.
        for c in r.get("crashes", []):
            age = c.get("hit_age", 0.0)
            age_txt = f" age={age:.1f}s" if age else ""
            print(f"         ↳ {c.get('label', '?'):<6} "
                  f"killed_by={c.get('cause', '?'):<22}"
                  f"@ ({c.get('x', 0):.1f}, {c.get('y', 0):.1f}) "
                  f"at t={c.get('at_t', 0):.1f}s{age_txt}")


def summarize_round(logotron: Path, meta: dict) -> str:
    """Narrative for one round — distills the JSONL into 'what happened'."""
    round_num = meta.get("round", 0)
    ticks_path = logotron / f"round_{round_num:03d}.jsonl"
    if not ticks_path.exists():
        return "(no tick file)"
    ever_saw = False
    first_sight_t = None
    first_turn_t = None
    last_tick = None
    first_dir = None
    turns = 0
    for line in ticks_path.read_text().splitlines():
        if not line.strip():
            continue
        try:
            t = json.loads(line)
        except json.JSONDecodeError:
            continue
        if first_dir is None:
            first_dir = t.get("dir")
        if t.get("opp_vis"):
            if not ever_saw:
                first_sight_t = t.get("t")
            ever_saw = True
        if last_tick and t.get("chose") != last_tick.get("chose"):
            turns += 1
            if first_turn_t is None:
                first_turn_t = t.get("t")
        last_tick = t
    bits = []
    bits.append(f"first_turn={first_turn_t:.1f}s" if first_turn_t is not None else "no_turns")
    if ever_saw:
        bits.append(f"sighted_op@{first_sight_t:.1f}s")
    else:
        bits.append("never_saw_op")
    bits.append(f"turns={turns}")
    return ", ".join(bits)


def dump_round(session_dir: Path, round_num: int) -> None:
    logotron = session_dir / "logotron"
    meta_path = logotron / f"round_{round_num:03d}_meta.json"
    ticks_path = logotron / f"round_{round_num:03d}.jsonl"
    if meta_path.exists():
        print("META:", meta_path.read_text().strip())
        print()
    if not ticks_path.exists():
        print(f"No tick file at {ticks_path}", file=sys.stderr)
        sys.exit(2)
    print(f"{'t':>6}  {'x':>5} {'y':>5}  dir  head   lethal_N  "
          f"lethal_E  lethal_S  lethal_W  opp  chose")
    for line in ticks_path.read_text().splitlines():
        if not line.strip():
            continue
        t = json.loads(line)
        ln = t.get("lethal_n", 0)
        le = t.get("lethal_e", 0)
        ls = t.get("lethal_s", 0)
        lw = t.get("lethal_w", 0)
        # Render 1e9 sentinel as "--" so the table reads clean.
        def fmt(v):
            return "   --  " if v >= 1e8 else f"{v:7.2f}"
        print(f"{t.get('t', 0):6.2f}  "
              f"{t.get('x', 0):5.2f} {t.get('y', 0):5.2f}  "
              f"{t.get('dir', '?'):>3}  "
              f"{t.get('head_cur', 0):5.2f}  "
              f"{fmt(ln)}  {fmt(le)}  {fmt(ls)}  {fmt(lw)}  "
              f"{'Y' if t.get('opp_vis') else 'N'}    {t.get('chose', '?')}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("session", nargs="?", help="session directory")
    ap.add_argument("round", nargs="?", type=int, help="round number")
    args = ap.parse_args()

    if not args.session:
        list_sessions()
        return

    session_dir = Path(args.session).expanduser().resolve()
    if not session_dir.exists():
        # Maybe the user passed a session name only; try resolving
        # under the root.
        for p in SESSIONS_ROOT.rglob(args.session):
            session_dir = p
            break
    if not session_dir.exists():
        print(f"session not found: {args.session}", file=sys.stderr)
        sys.exit(2)

    if args.round is None:
        summarize_session(session_dir)
    else:
        dump_round(session_dir, args.round)


if __name__ == "__main__":
    main()
