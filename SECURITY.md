# Security Policy

## Supported versions

Logosphere is pre-1.0. Security fixes are made on `main` and included in
the next release. Only the current `main` branch and latest tagged release
are supported. Older releases do not receive separate security backports
unless the maintainer states otherwise in a release notice.

## Reporting a vulnerability

Do not open a public issue for an undisclosed vulnerability.

Email `hello@kieleth.com` with the subject
`[Logosphere security] <short description>`. Include:

- The affected commit or version.
- The affected component and build profile.
- Reproduction steps or a minimal proof of concept.
- Expected impact.
- Known mitigations or workarounds.
- Whether and when you intend to disclose the issue.

Do not include credentials, personal data, or unrelated private material.
Use synthetic test data whenever possible.

The maintainer will verify the report and coordinate remediation and
disclosure with the reporter. There is no fixed response-time guarantee
while the project has one maintainer.

## Scope

Security reports include memory-safety defects, arbitrary code execution,
path traversal, unsafe parsing at trust boundaries, credential exposure,
dependency compromise, and vulnerabilities in network-facing integrations.

Crashes, rendering defects, gameplay behavior, and performance regressions
without a security impact belong in the public issue tracker after the
repository is published.

