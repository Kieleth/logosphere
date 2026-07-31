# Governance

## Current model

Logosphere uses a maintainer-led governance model while it is pre-1.0.
Kieleth is the current maintainer and release authority.

The maintainer decides project direction, public API, architecture,
ontology boundaries, supported platforms, release contents, security
responses, licensing, and whether a contribution is accepted.

## Changes

Changes are proposed through pull requests. Public API and behavior changes
require tests and a changelog entry. Ontology changes must update the source
schema and regenerated outputs together. Releases follow the repository's
release checklist and require a clean working tree.

Discussion does not imply acceptance. The engine provides generic mechanism;
games provide policy. Contributions that cross that boundary may be declined
even when technically correct.

## Contributions and rights

External code contributions are merged only with a DCO sign-off and
under the Contribution License Grant published in CONTRIBUTING.md,
enforced by the sign-off check in CI. The grant framework will be
reviewed by counsel before the first commercial royalty agreement is
executed.

The intended contributor model lets contributors retain ownership of their
work while granting the project enough copyright and patent rights to
distribute accepted contributions under the project's community and
commercial terms. The final contributor agreement controls if this summary
differs from it.

## Releases

Only the maintainer may publish a release or change the repository's license.
Tags do not become public releases merely because they exist in the private
repository. Publication requires explicit approval of the corresponding
source, artifacts, release notes, and license status.

## Evolution

If regular maintainers or a contributor community form, this document should
be replaced with explicit roles, nomination and removal rules, review
requirements, decision records, and a conflict-resolution process.

