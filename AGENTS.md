# AGENTS.md

Context for AI coding assistants working on flux-core.

## Key documentation resources

**Read these before diving into code** — they provide essential context in
natural language that is much faster to absorb than source reading.

- `doc/` — Man pages (`doc/man1/`, `doc/man3/`, `doc/man5/`, `doc/man7/`) and
  narrative guides (`doc/guide/`). The guide covers administration, broker
  internals, the KVS, job workflows, and more. `doc/man7/` has conceptual
  overviews of the jobtap plugin system, shell plugins, and broker attributes.

- `../flux-rfc/` or `../rfc/` — Flux RFCs (spec_NN.rst). The canonical repo
  name is `rfc` under the flux-framework GitHub organization; `flux-rfc` is a
  common local checkout name. Check both paths. These are the authoritative
  specifications for wire protocols, data formats, job schema, constraints,
  resource graph formats, and coding/contribution standards. Key ones:
  - RFC 1: contribution process (C4.1)
  - RFC 7: coding style
  - RFC 14: job schema / jobspec
  - RFC 20: resource set representation (R)
  - RFC 22: constraint syntax used in jobspec
  - RFC 33: flux job-list service

## Repository layout

```
src/
  bindings/python/flux/   Python bindings and utilities (UtilConfig, OutputFormat, etc.)
  broker/                 flux-broker daemon
  cmd/                    CLI commands (flux-resource.py, flux-jobs.py, etc.)
  common/                 Shared C libraries (libflux, libutil, etc.)
  job-manager/            Job manager service and jobtap plugin infrastructure
  modules/                Broker modules (kvs, job-ingest, sched-simple, etc.)
  shell/                  flux-shell and shell plugin infrastructure
doc/
  guide/                  Narrative documentation (RST)
  man1/ man3/ man5/ man7/ Man pages (RST source)
t/                        Testsuite (sharness shell scripts, Python, Lua)
  README.md               How to write and run tests
```

## Local development notes

See README.md for the full list of related repositories and guidance on
always invoking `flux` and `flux python` from the local build. Sister
repositories are typically checked out as siblings, e.g. `../flux-sched`,
`../flux-coral2`, `../flux-accounting`, `../flux-pmix`, `../flux-security`.

## Building and testing

```sh
./autogen.sh        # only needed when building from git
./configure
make -jN
make -jN check      # runs full testsuite; -j recommended, it's large
```

See README.md for how to run individual test scripts and useful debug flags.

`FLUX_HANDLE_TRACE=t` dumps all Flux messages on a handle to stderr — useful
when debugging a single command, but do not set it when running sharness tests
that use `test_under_flux` as it floods output from the entire Flux instance.

See `t/README.md` for details on the sharness framework and test patterns.

## Coding conventions

- C code follows RFC 7 (flux-framework coding style). See `../flux-rfc/spec_7.rst`.
- Shell scripts must be executable (`chmod +x`) and have a shebang line.
  Use `#!/bin/sh` rather than `#!/bin/bash` for maximum portability unless
  bash-specific features are required. The pre-commit `check-shebang-scripts-are-executable`
  hook will catch missing shebangs on executable files.
- Python code is checked by pre-commit hooks: `black` (formatting), `isort`
  (import ordering), `flake8` (linting), `mypy` (via `scripts/run_mypy.sh`),
  and `vermin` (minimum Python 3.6 compatibility). Run `pre-commit run --files
  <file>` before committing to catch issues early. The hooks apply to files
  under `src/bindings/python/flux`, `src/cmd`, `t/rc`, `t/python`,
  `t/scripts/*.py`, and `etc/`.

## Commit message format (RFC 1)

All commits must follow RFC 1 (C4.1). Key requirements:

- **Title**: 50 characters or less, imperative mood, no trailing period.
  MAY use a `tag: short description` prefix to denote the area of change,
  e.g. `flux-resource: add hidden-queues config option`.
- **Blank line** between title and body.
- **Body**: wrapped at 72 characters (except non-prose lines).
- **Body SHOULD begin with `Problem:`** followed by a short paragraph
  describing the problem the commit solves. Feature additions may also
  use a Problem statement.
- Body should describe what changed and why.
- Reference issues where applicable, e.g. `Fixes #123`.
- Each commit should be a minimal, accurate answer to exactly one problem.

Example:
```
flux-resource: add hidden-queues config option

Problem: on large systems with catch-all queues, the default output of
`flux resource list` is cluttered with queue names that are not useful
to most users.

Add a `hidden-queues` option to the `[list]` table in flux-resource.toml
that suppresses specified queues from default output. Hidden queues are
still shown when explicitly requested via -q.

Fixes #1234
```

## Contribution process

See `CONTRIBUTING.md` and RFC 1. PRs should reference an issue, commits
should be self-contained and well-described, and new features should
include tests and documentation updates.

Topic branches for work in progress must never be pushed to the
flux-framework/flux-core repository directly — they may only exist in a
contributor's private fork. Changes reach the upstream repo only via pull
request.

Branches are typically forked off master, but a branch may also be based
on top of another live PR/branch. When squashing or collapsing commits,
never collapse commits from before the start of the current topic branch —
doing so would rewrite commits belonging to the upstream branch.
