#!/usr/bin/env python3
"""Refuse to generate ontology outputs from an environment nobody declared.

WHY THIS EXISTS, and it is not hypothetical. Measured 2026-08-20 on the
dev machine:

  environment.yml declares an environment named `logosphere`.
      That environment did not exist.
  What existed was `logosphere_env`, which had no linkml at all
      (`import linkml_runtime` -> ModuleNotFoundError).
  The only interpreter on the machine with linkml was `base`, carrying
      an editable install reporting 1.10.0.post230.dev0+2909900a4 —
      a version string that does not satisfy this repository's own
      `linkml >=1.11.0` pin.

So every ontology regeneration on that machine ran from `base`, against
an unpinned editable checkout, while CI ran against the pinned file.
The generator is the thing that writes COMMITTED SOURCE. A generator
and a CI lane disagreeing about their inputs is the one drift with
nothing downstream to catch it.

environment.yml carried a comment blessing exactly that ("a strict
resolver on that machine must use the editable install as-is"). A
comment is not a mechanism, and that one was also false: `conda env
create -f environment.yml` resolves linkml 1.11.1 from conda-forge on
osx-arm64 and on linux-64 alike, and the generator then runs to zero
drift. The comment is deleted and this file replaces it.

WHAT IT CHECKS, all of it read out of environment.yml so there is one
source of truth and no second declaration to drift:

  1. linkml is importable at all
  2. its version satisfies the declared floor
  3. it is a FINAL release — not .devN, not a pre-release, not a
     +local build. That is the editable-checkout case by name.
  4. the interpreter's python matches the declared pin

WHAT IT DELIBERATELY DOES NOT CHECK: the environment's NAME. CI
materialises environment.yml under whatever name its setup action
chooses (`test`, today), and a name check would refuse a run that is
correct in every way that matters. The contents are the contract.

THERE IS NO OVERRIDE. An escape hatch here is the comment this file
replaces, wearing an env var.
"""
from __future__ import annotations

import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parent.parent
ENV_FILE = REPO / "environment.yml"

_UNSET = object()


def declared(env_file: Path | None = None) -> dict:
    """The floors this repository declares, read from environment.yml."""
    env_file = env_file or ENV_FILE
    spec = yaml.safe_load(env_file.read_text())
    out = {"name": spec.get("name"), "python": None, "linkml_floor": None}
    for entry in spec.get("dependencies") or []:
        if not isinstance(entry, str):
            continue
        text = entry.strip()
        if text.startswith("python") and "=" in text:
            out["python"] = text.split("=", 1)[1].strip()
        elif text.startswith("linkml") and ">=" in text:
            # `linkml >=1.11.0`, not `linkml-runtime`.
            name, floor = text.split(">=", 1)
            if name.strip() == "linkml":
                out["linkml_floor"] = floor.strip()
    return out


def problems(*, linkml_version, python_version, floor, python_pin) -> list:
    """Pure. `linkml_version` is None when linkml is not importable."""
    found = []

    if python_pin and python_version:
        if not (python_version == python_pin
                or python_version.startswith(python_pin + ".")):
            found.append(
                f"python {python_version} is not the declared {python_pin}. "
                f"Generated output can differ between interpreters and "
                f"nothing downstream compares them.")

    if linkml_version is None:
        found.append(
            "linkml is not installed in this interpreter. The generator "
            "needs linkml.generators.cppgen, which linkml-runtime alone "
            "does not provide.")
        return found

    if not floor:
        found.append(
            f"{ENV_FILE.name} declares no linkml floor, so nothing here can "
            f"be checked. Restore the pin.")
        return found

    try:
        from packaging.version import InvalidVersion, Version
    except ImportError:
        found.append(
            "packaging is not importable, so the linkml version cannot be "
            "compared. It ships with the declared environment.")
        return found

    try:
        version = Version(linkml_version)
    except InvalidVersion:
        found.append(f"linkml reports {linkml_version!r}, which is not a "
                     f"version this can compare.")
        return found

    if version < Version(floor):
        found.append(
            f"linkml {linkml_version} is BELOW the declared floor {floor}. "
            f"linkml.generators.cppgen first shipped in 1.11.0; below it, "
            f"ontology codegen cannot run at all.")
    if version.is_devrelease or version.is_prerelease or version.local:
        found.append(
            f"linkml {linkml_version} is a development build, not a release "
            f"(this is what an editable checkout reports). The generator "
            f"writes committed source, so its input has to be a version CI "
            f"can resolve too.")
    return found


def _installed_linkml_version():
    try:
        from importlib.metadata import PackageNotFoundError, version
    except ImportError:  # pragma: no cover - python <3.8 is not supported
        return None
    try:
        return version("linkml")
    except PackageNotFoundError:
        return None


def enforce(env_file: Path | None = None, *,
            linkml_version=_UNSET, python_version=_UNSET) -> None:
    """Print and exit(2) unless this interpreter is the declared one.

    The two version arguments exist so the gate itself can be tested in
    both directions without installing a broken linkml. Nothing in the
    shipping path passes them.
    """
    env_file = env_file or ENV_FILE
    spec = declared(env_file)

    if linkml_version is _UNSET:
        linkml_version = _installed_linkml_version()
    if python_version is _UNSET:
        python_version = f"{sys.version_info.major}.{sys.version_info.minor}"

    found = problems(linkml_version=linkml_version,
                     python_version=python_version,
                     floor=spec["linkml_floor"],
                     python_pin=spec["python"])
    if not found:
        return

    name = spec["name"] or "logosphere"
    print("", file=sys.stderr)
    print("ONTOLOGY GENERATOR REFUSED TO RUN.", file=sys.stderr)
    print("", file=sys.stderr)
    print("It writes committed source. Running it outside the declared "
          "environment lets", file=sys.stderr)
    print("the tree disagree with what CI regenerates, with nothing to say "
          "so.", file=sys.stderr)
    print("", file=sys.stderr)
    print(f"  {env_file.name} declares  python {spec['python']}, "
          f"linkml >={spec['linkml_floor']}", file=sys.stderr)
    print(f"  this interpreter has    python {python_version}, "
          f"linkml {linkml_version or '(not installed)'}", file=sys.stderr)
    print(f"                          {sys.executable}", file=sys.stderr)
    print("", file=sys.stderr)
    for problem in found:
        print(f"  - {problem}", file=sys.stderr)
    print("", file=sys.stderr)
    print("Fix, and there are no manual steps in it:", file=sys.stderr)
    print("", file=sys.stderr)
    print(f"  conda env create -f {env_file.name}          # once", file=sys.stderr)
    print(f"  conda run -n {name} python scripts/generate_ontology.py",
          file=sys.stderr)
    print("", file=sys.stderr)
    print("There is no override. This gate replaces a comment in "
          f"{env_file.name} that", file=sys.stderr)
    print("blessed running from an editable install, and an env var would "
          "be the same", file=sys.stderr)
    print("comment wearing different clothes.", file=sys.stderr)
    sys.exit(2)


if __name__ == "__main__":
    enforce()
    spec = declared()
    print(f"environment: python {sys.version_info.major}."
          f"{sys.version_info.minor}, linkml {_installed_linkml_version()} "
          f"(declared: python {spec['python']}, linkml >={spec['linkml_floor']})")
