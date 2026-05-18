###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Multi-benchmark sweep runner for flux schedbench.

A sweep generates a cross-product of benchmark configurations from a
parameter matrix and dispatches each combination as a child
``flux schedbench run`` invocation.  Results are appended to the
same flat results file as single runs, each tagged with a ``sweep``
sub-object recording the run's axis values and position in the
matrix.

This is a single self-contained module combining the three concerns
of the sweep feature:

* **Matrix layer** — parsing input (CLI flags or TOML file),
  expanding the cross-product, and producing :class:`RunSpec`
  instances ready for dispatch.  (:func:`parse_rfc45_range`,
  :func:`parse_axis_spec`, :class:`RunSpec`, :class:`SweepMatrix`.)

* **UI layer** — :class:`TerminalSweepEmitter` (live TTY dashboard
  with progress bars, status counts, active and recent run
  sections) and :class:`LineSweepEmitter` (plain text, one line
  per state transition, for non-TTY output).  Same flicker
  mitigation as the single-run UI (DEC 2026 + per-line erase,
  single-write frames, cursor-hide, 20Hz throttle).

* **Dispatch layer** — :func:`run_flux` submits each run as a
  Flux job to the outer instance, running them in parallel via a
  single :class:`flux.job.JobWatcher`.  Children consume their
  :class:`RunSpec` instances from :meth:`SweepMatrix.expand`;
  per-run scheduler-recipe env overlays are applied via
  ``JobspecV1.from_submit``'s env filter rules.  The dispatcher
  parses each child's JSON event stream into emitter callbacks
  and writes sweep-tagged records to the shared results file.
  Sweeps without an outer Flux instance can be run under
  ``flux start -s 1 -- flux schedbench sweep ...``.
"""

import getpass
import itertools
import json
import os
import random
import re
import shlex
import socket
import sys
import time

# tomllib added to standard library in Python 3.11
# flux-core minimum is Python 3.6.
try:
    import tomllib  # novermin
except ModuleNotFoundError:
    from flux.utils import tomli as tomllib

try:
    from dataclasses import dataclass, field  # novermin
except ModuleNotFoundError:
    from flux.utils.dataclasses import dataclass, field

from flux.testing.schedbench import BENCHMARKS, BenchmarkResults
from flux.testing.schedbench._ansi import (
    _BAR_CHARS_ASCII,
    _BAR_CHARS_UNICODE,
    _BOLD,
    _CURSOR_HIDE,
    _CURSOR_SHOW,
    _CURSOR_UP_FMT,
    _DIM,
    _ERASE_TO_EOL,
    _FG_CYAN,
    _FG_GREEN,
    _FG_RED,
    _GLYPHS_ASCII,
    _GLYPHS_UNICODE,
    _REDRAW_INTERVAL_S,
    _RESET,
    _SYNC_BEGIN,
    _SYNC_END,
    _color_supported,
    _isatty,
    _safe_glyphs,
)

# ===========================================================
# Matrix layer
# ===========================================================
#: RFC 45 operators we recognize.  ``+`` is additive (step by operand),
#: ``*`` is multiplicative (multiply by operand each step), ``^`` is
#: exponential (raise to the operand each step).  Maps the operator
#: char to a function ``(value, operand) -> next_value``.
_RFC45_OPS = {
    "+": lambda v, op: v + op,
    "*": lambda v, op: v * op,
    "^": lambda v, op: v**op,
}


def parse_rfc45_range(spec):
    """Parse an RFC 45 range string into a list of values.

    Forms (per `RFC 45 <https://flux-framework.readthedocs.io/projects/
    flux-rfc/en/latest/spec_45.html>`_)::

        min-max                # additive, step 1
        min-max:operand        # additive, step operand
        min-max:operand:op     # operator op (+, *, ^)
        [min-max:operand:op]   # brackets optional

    Open-ended forms (``min+:operand:op``) are rejected — a sweep
    needs a finite list.

    Returns a list of ints in ascending order.  Raises
    :class:`ValueError` for malformed input or unknown operators.
    """
    s = spec.strip()
    if s.startswith("[") and s.endswith("]"):
        s = s[1:-1].strip()
    if "+" in s.split(":", 1)[0]:
        # The first colon-separated field is the range; '+' there
        # would mean open-ended.
        raise ValueError(
            f"open-ended RFC 45 ranges not allowed in a sweep "
            f"(spec must be finite): {spec!r}"
        )
    parts = s.split(":")
    if len(parts) > 3:
        raise ValueError(f"too many colons in RFC 45 spec: {spec!r}")
    if "-" not in parts[0]:
        raise ValueError(f"missing min-max in RFC 45 spec: {spec!r}")
    lo_s, hi_s = parts[0].split("-", 1)
    try:
        lo = int(lo_s)
        hi = int(hi_s)
        operand = int(parts[1]) if len(parts) > 1 else 1
    except ValueError as exc:
        raise ValueError(f"non-integer field in RFC 45 spec: {spec!r}") from exc
    op = parts[2] if len(parts) > 2 else "+"
    if op not in _RFC45_OPS:
        raise ValueError(
            f"unknown RFC 45 operator {op!r} in {spec!r}; "
            f"valid operators: {sorted(_RFC45_OPS)}"
        )
    if lo > hi:
        raise ValueError(f"min > max in RFC 45 spec: {spec!r}")
    step = _RFC45_OPS[op]
    out = []
    v = lo
    while v <= hi:
        out.append(v)
        nxt = step(v, operand)
        if nxt <= v:
            # Operand of 1 with * or 0 with + would never advance.
            # Don't loop forever; treat as "single-value range."
            break
        v = nxt
    return out


# Heuristic: spec looks like RFC 45 if it has range syntax (``-`` or
# ``:`` or surrounding brackets) and contains no commas.  Anything
# else is a comma-separated discrete list.  ``sched-simple`` and
# similar hyphenated string values fall through to the comma-list
# path because non-numeric characters break the regex below.
_RFC45_LIKE = re.compile(r"^\[?\d+-\d+(:\d+(:[+*^])?)?\]?$")


def parse_axis_spec(spec):
    """Parse an axis SPEC into a list of values.

    Accepts:

    * RFC 45 range strings (``1-8``, ``1-32:8``, ``1-1024:2:*``,
      ``[1-256:2:^]``) — expanded numerically.
    * Comma-separated lists (``sched-simple,sched-fluxion-qmanager``
      or ``100,1000,10000``) — each element coerced to int / float
      / bool / str.
    * A bare single value with no commas or range syntax — returns
      a one-element list.

    Coercion: ``true``/``false`` (case-insensitive) → bool, decimal
    integers → int, decimals with ``.`` → float, anything else →
    str.  Strings that look numeric but mean labels can be quoted
    at the shell level.
    """
    spec = spec.strip()
    if _RFC45_LIKE.match(spec):
        return parse_rfc45_range(spec)
    return [_coerce(v.strip()) for v in spec.split(",")]


def _coerce(v):
    """Best-guess coercion for a single string axis value."""
    if v.lower() in ("true", "yes"):
        return True
    if v.lower() in ("false", "no"):
        return False
    try:
        return int(v)
    except ValueError:
        pass
    try:
        return float(v)
    except ValueError:
        pass
    return v


def _coerce_param_value(raw):
    """Coerce one top-level TOML parameter value into a list of
    one-or-more axis values.  Centralizes the shape handling for
    :meth:`SweepMatrix.from_file`:

    * ``list`` (TOML array) -> values as-is (each coerced if str)
    * ``str`` -> parsed as RFC 45 range, comma list, or single
      value via :func:`parse_axis_spec`
    * ``int``/``float``/``bool`` -> one-element list

    Always returns a list, even for the single-value case, so
    callers can uniformly check ``len(values) == 1`` to decide
    fixed vs axis.
    """
    if isinstance(raw, list):
        return [_coerce(v) if isinstance(v, str) else v for v in raw]
    if isinstance(raw, str):
        return parse_axis_spec(raw)
    return [raw]


def _make_scheduler_recipe(name, options=None):
    """Build a simple scheduler recipe from a name.

    Used for CLI-provided scheduler axis values like
    ``--scheduler=sched-simple,sched-backfill`` where there's no
    ``[[scheduler]]`` block to consult.  ``module`` defaults to
    ``name``: a CLI ``--scheduler=sched-backfill`` is shorthand
    for "force-load ``sched-backfill``", matching the CLI's
    existing semantic.  ``options`` becomes the args entry for
    that module, threading the existing
    ``--scheduler-options=OPTS`` CLI flag through the recipe.

    Returns a dict matching :func:`_recipe_from_block`'s shape so
    the dispatch layer can treat both uniformly — ``env`` is a
    list of :meth:`JobspecV1.from_submit`-style filter rules
    (empty for the CLI-synthesized case).
    """
    modules = []
    if options:
        modules.append({"name": name, "options": options})
    return {
        "name": name,
        "module": name,
        "modules": modules,
        "conf": {},
        "env": [],
    }


#: Allowed top-level keys in a ``[[scheduler]]`` block.  Used by
#: :func:`_recipe_from_block` to reject misscoped keys (a common
#: TOML pitfall: a key placed after a ``[[scheduler.modules]]``
#: header attaches to the module entry instead of the parent
#: scheduler).  Hard error rather than a warning since the dispatch
#: UI swallows stderr noise — silent misscoping has bitten us.
_KNOWN_SCHEDULER_KEYS = frozenset(
    {
        "name",
        "module",
        "modules",
        "conf",
        "env",
    }
)

#: Allowed keys in a ``[[scheduler.modules]]`` entry.  Anything
#: else almost certainly means a parent-scoped key got captured
#: by this sub-table; raise rather than silently drop.
_KNOWN_MODULE_KEYS = frozenset({"name", "options"})


def _recipe_from_block(block):
    """Normalize a TOML ``[[scheduler]]`` block into a recipe dict.

    Required field is ``name`` (the axis value / display label).
    All other fields are optional and refine the recipe:

    * ``module`` -> scheduler module to force-load via
      ``modules.alternatives.sched``.  When omitted, the default
      depends on whether the block is in *shorthand form* (just
      ``name``, no other config) or *full form* (with ``modules``
      / other config):

      - **Shorthand** (``[[scheduler]] name = "sched-backfill"``):
        ``module`` defaults to ``name`` — equivalent to the CLI
        ``--scheduler=sched-backfill``.
      - **Full form** (``modules`` set, no ``module``): no
        alternative override.  ``--scheduler`` is not emitted;
        modprobe loads whatever it would load by default
        (typically Fluxion on a system with its modprobe config
        on the path).  Per-module args still apply.

      To opt out of the shorthand default explicitly when no
      other config is present, set ``module = ""``.

    * ``modules`` -> list of ``{name, options}`` per-module args
      overrides.  Does NOT dictate load order — modprobe handles
      dependencies and load order from its own config.  ``options``
      threads through as ``--conf=modules.<name>.args=[...]``.
    * ``conf`` -> dict of runtime conf entries (read by modules
      via ``flux_conf_get()``, distinct from load-time args).
    * ``env`` -> list of :meth:`flux.job.JobspecV1.from_submit`
      env filter rules applied to the submitter's environment:

      - ``"VAR=VALUE"`` sets *VAR*, expanding ``$VAR`` references
        against the env built so far (so rules can reference
        earlier rules: define ``FLUXION_HOME=…`` then use
        ``FOO=$FLUXION_HOME/…``).
      - ``"VAR"`` (or glob ``"SLURM_*"``) imports from the
        submitter env.
      - ``"-PATTERN"`` removes vars matching glob *PATTERN*.
      - ``"^/path"`` reads additional rules from a file.

      As a convenience, a dict ``env = { K = "V", ... }`` is
      accepted and converted to ``["K=V", ...]`` rules — handy
      for the common "set these vars" case without ``$VAR``
      cross-references.  Submitter env is preserved by default
      (no rules = no filtering); rules ADD or REMOVE selectively.

    Each module entry must have at least ``name``; ``options`` is
    optional.
    """
    if "name" not in block:
        raise ValueError("[[scheduler]] block missing 'name'")
    name = block["name"]
    unknown = set(block) - _KNOWN_SCHEDULER_KEYS
    if unknown:
        raise ValueError(
            f"[[scheduler]] {name!r}: unknown key(s) "
            f"{sorted(unknown)}.  Known keys: "
            f"{sorted(_KNOWN_SCHEDULER_KEYS)}.  This often "
            f"means a key got misscoped under a sub-table — "
            f"TOML attaches keys to the most recently-opened "
            f"table, so a top-level key placed after a "
            f"[[scheduler.modules]] or [scheduler.env] header "
            f"ends up nested inside that sub-table.  Move the "
            f"key above any sub-table headers."
        )
    if "module" in block:
        module = block["module"] or None
    elif "modules" in block:
        module = None
    else:
        module = name
    modules = [dict(m) for m in (block.get("modules") or [])]
    for m in modules:
        if "name" not in m:
            raise ValueError(f"[[scheduler]] {name!r}: module entry missing " f"'name'")
        unknown_mod = set(m) - _KNOWN_MODULE_KEYS
        if unknown_mod:
            raise ValueError(
                f"[[scheduler]] {name!r}: module entry "
                f"{m['name']!r} has unknown key(s) "
                f"{sorted(unknown_mod)}.  Known keys: "
                f"{sorted(_KNOWN_MODULE_KEYS)}.  This usually "
                f"means a parent-scoped key got captured by "
                f"the [[scheduler.modules]] sub-table — TOML "
                f"attaches keys to the most recently-opened "
                f"table.  Move the key above the "
                f"[[scheduler.modules]] header(s)."
            )
    raw_env = block.get("env") or []
    if isinstance(raw_env, dict):
        # Convenience: dict form -> ["K=V"] rules.  Quoting V is
        # the caller's problem; we pass through verbatim so $VAR
        # expansion (handled by from_submit's rule engine) still
        # works on values originating from the dict form.
        env = [f"{k}={v}" for k, v in raw_env.items()]
    elif isinstance(raw_env, list):
        for entry in raw_env:
            if not isinstance(entry, str):
                raise ValueError(
                    f"[[scheduler]] {name!r}: env entries must be "
                    f"strings (got {type(entry).__name__})"
                )
        env = list(raw_env)
    else:
        raise ValueError(
            f"[[scheduler]] {name!r}: env must be a list of "
            f"rule strings or a dict (got "
            f"{type(raw_env).__name__})"
        )
    return {
        "name": name,
        "module": module,
        "modules": modules,
        "conf": dict(block.get("conf") or {}),
        "env": env,
    }


def _format_conf_value(value):
    """Render a TOML-parsed value as the right-hand side of a
    ``--conf=KEY=VALUE`` flag.

    flux start's ``--conf=`` parser accepts TOML/JSON syntax on
    the RHS, so:

    * ``bool`` -> ``true`` / ``false`` (TOML literal, lowercase)
    * ``list`` / ``dict`` -> JSON-encoded (a valid TOML/JSON
      inline array or table — both parse identically here).
      Without this, ``str(list)`` produces Python-repr output
      with single quotes that's neither valid TOML nor JSON and
      flux start rejects it.
    * everything else -> ``str(value)`` (numbers and strings
      round-trip fine).

    Used for both the file's ``[conf]`` section and per-recipe
    ``conf`` dicts on ``[[scheduler]]`` blocks.
    """
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (list, dict)):
        return json.dumps(value)
    return str(value)


@dataclass
class RunSpec:
    """One row of an expanded sweep matrix.

    Attributes:
        axes: ordered dict mapping axis name -> value for this run
        fixed: non-axis parameters (applied to every run in the sweep)
        run_index: 0-based position in the expansion
        total: total number of runs in the expansion (for display)
        conf: list of user-supplied ``--conf KEY=VALUE`` strings
            (applied to every run, like ``fixed`` but forwarded as
            ``--conf=`` flags rather than as native run options)
        varying_axes: set of axis names that have more than one
            value in the parent matrix.  When set, the dashboard's
            per-row axis summary skips other axes (they're
            effectively-fixed, redundant ink).  Defaults to empty;
            in that case all axes show.
        axis_column_widths: per-axis maximum-formatted-value width,
            so :meth:`axis_summary` pads each value into a column
            and rows line up tabularly.  Populated by
            :meth:`SweepMatrix.expand`; defaults to empty (no
            per-column padding, just space-separated).
        scheduler_recipe: optional resolved scheduler recipe dict
            for this run (the ``[[scheduler]]`` block matched by
            this run's scheduler axis value, or a synthesized
            single-module recipe for a CLI-only name).  When
            present, :meth:`argv` emits the recipe's modules and
            runtime conf as child ``--conf`` flags; the dispatch
            layer reads ``recipe['env']`` for per-run environment
            overlays.  None for sweeps without any scheduler axis.
    """

    axes: dict
    fixed: dict
    run_index: int
    total: int
    conf: list = field(default_factory=list)
    varying_axes: set = field(default_factory=set)
    axis_column_widths: dict = field(default_factory=dict)
    scheduler_recipe: dict = field(default_factory=dict)

    def test_name(self):
        """Return the test (benchmark) name for this run.  Resolved
        from the ``test`` axis if present, falling back to the
        fixed-args ``test`` key.  Empty string if neither is set
        (which is an invalid config — every run needs a test).
        """
        return self.axes.get("test") or self.fixed.get("test", "")

    def axis_summary(self):
        """A short, human-readable summary of this run's axis
        values for the dashboard, with per-axis column padding so
        rows line up.

        Special-cases:

        * ``test`` is omitted (the identity column shows it).
        * ``scheduler`` values render bare (just the value, no
          ``scheduler=`` prefix, no ``sched-`` prefix) since
          scheduler names like ``simple`` and ``fluxion-qmanager``
          are unambiguous from context.
        * If ``varying_axes`` is set, any axis that's
          effectively-fixed across the matrix is omitted.
        * If ``axis_column_widths`` is set, each value is left-padded
          to its axis's column width — so ``simple`` (6 chars) and
          ``backfill`` (8 chars) both occupy 8-char columns and the
          next axis lines up across rows.
        """
        bits = []
        for k, v in self.axes.items():
            if k == "test":
                continue
            if self.varying_axes and k not in self.varying_axes:
                continue
            value = _format_axis_value(k, v)
            width = self.axis_column_widths.get(k, 0)
            bits.append(value.ljust(width))
        return "  ".join(bits)

    def argv(self):
        """Generate the argv list for ``flux schedbench run`` that
        executes this run.  Does not include the ``flux schedbench``
        prefix or any UI/output flags — the dispatch layer adds
        those uniformly across all runs.

        The ``test`` axis (or fixed ``test`` value) becomes the
        positional argument; other axes and fixed values become
        ``--flag value`` pairs using the canonical long-form name.

        ``scheduler`` and ``scheduler_options`` are special-cased:
        instead of emitting them naively, the resolved
        :attr:`scheduler_recipe` (when present) drives the
        scheduler arguments.  The recipe carries an optional
        ``module`` (which module to force-load, defaulting to the
        recipe's ``name``; maps to ``--scheduler=NAME`` and thence
        to ``--conf=modules.alternatives.sched=NAME``) plus a list
        of per-module args overrides and runtime ``conf`` entries.
        modprobe handles the actual module loading, dependencies,
        and load order from its own config — schedbench only
        expresses overrides to that, never the load plan itself.

        For a single-module recipe synthesized from CLI
        ``--scheduler=NAME --scheduler-options=OPTS``, this reduces
        to ``--scheduler=NAME --conf=modules.NAME.args=[OPTS]``.
        For a multi-module recipe (e.g. Fluxion with custom args
        for both qmanager and resource), each module's options
        become a ``--conf=modules.<name>.args=[...]`` entry; the
        recipe's ``module`` picks which module advertises ``sched``.
        """
        params = {**self.fixed, **self.axes}
        test = params.pop("test", None)
        if test is None:
            raise ValueError("RunSpec missing required 'test' axis or fixed value")
        # Scheduler args come from the recipe — pop the raw values
        # so the generic loop below doesn't double-emit them.
        params.pop("scheduler", None)
        params.pop("scheduler_options", None)

        argv = ["run", str(test)]
        for k, v in params.items():
            flag = "--" + k.replace("_", "-")
            if isinstance(v, bool):
                # Boolean axes turn into a flag-or-nothing.  The
                # only such axis we currently care about is
                # ``real_exec`` -> ``--exec``.
                if k == "real_exec" and v:
                    argv.append("--exec")
                elif v:
                    argv.append(flag)
                # False bool: omit (CLI default is False)
            else:
                argv.extend([flag, str(v)])

        # Scheduler from recipe.  modprobe handles the actual
        # module loading and dependency resolution — including
        # which scheduler module gets loaded by default (driven by
        # priority and whatever ``FLUX_MODPROBE_PATH_APPEND``
        # points at).  The recipe's role is to express
        # *overrides*: ``module`` says which scheduler to
        # force-load (mapping to ``modules.alternatives.sched``)
        # and defaults to the recipe's ``name`` — so a simple
        # block like ``[[scheduler]] name = "sched-backfill"``
        # behaves exactly like CLI ``--scheduler=sched-backfill``.
        # The ``modules`` list provides per-module args overrides;
        # ``conf`` provides runtime conf entries.
        recipe = self.scheduler_recipe
        if recipe:
            if recipe.get("module"):
                argv.extend(["--scheduler", str(recipe["module"])])
            for mod in recipe.get("modules", []):
                if mod.get("options"):
                    opts_list = shlex.split(mod["options"])
                    argv.extend(
                        [
                            "--conf",
                            f"modules.{mod['name']}.args=" f"{json.dumps(opts_list)}",
                        ]
                    )
            for key, value in recipe.get("conf", {}).items():
                argv.extend(
                    [
                        "--conf",
                        f"{key}={_format_conf_value(value)}",
                    ]
                )

        for entry in self.conf:
            argv.extend(["--conf", entry])
        return argv


class SweepMatrix:
    """Expanded representation of a sweep's parameter matrix.

    Holds the axis declarations (ordered: declaration order ==
    cross-product nesting order) and the fixed non-axis parameters.
    Call :meth:`expand` to enumerate :class:`RunSpec` instances.

    Instances are typically built via :meth:`from_cli` or
    :meth:`from_file`; the constructor is exposed mainly for tests.
    """

    def __init__(self, axes, fixed, name=None, conf=None, scheduler_recipes=None):
        self.axes = dict(axes)
        self.fixed = dict(fixed)
        self.name = name or _default_sweep_name()
        self.conf = list(conf or [])
        #: name -> recipe dict ({name, module, modules, conf, env}).
        #: Populated by :meth:`from_file` from ``[[scheduler]]``
        #: blocks; consulted by :meth:`_resolve_recipe` when expanding
        #: to attach the matching recipe to each :class:`RunSpec`.
        #: Empty when no file recipes are defined (all schedulers
        #: fall back to simple single-module synthesis at expansion
        #: time).
        self.scheduler_recipes = dict(scheduler_recipes or {})

    def _resolve_recipe(self, name, scheduler_options=None):
        """Return the recipe dict for scheduler ``name``.

        File-defined ``[[scheduler]]`` recipes win — their
        ``module`` / ``modules`` / ``conf`` / ``env`` are the
        authoritative config and ``scheduler_options`` is ignored
        when one applies (the recipe carries its own per-module
        options).

        For names that don't match a file recipe (the common case
        of CLI-only schedulers like ``sched-simple``), synthesize
        a recipe via :func:`_make_scheduler_recipe`: ``module`` is
        the name (so ``--scheduler=NAME`` means force-load NAME,
        matching CLI semantics) and the optional
        ``scheduler_options`` becomes that module's args.
        """
        if name in self.scheduler_recipes:
            return self.scheduler_recipes[name]
        return _make_scheduler_recipe(name, options=scheduler_options)

    @classmethod
    def from_cli(cls, params, name=None, conf=None):
        """Build from already-parsed CLI parameter values.

        ``params`` is a dict mapping parameter name to a list of
        values.  Single-element lists become fixed parameters;
        multi-element lists become sweep axes.  This is the shape
        produced by :func:`cmd_sweep` after running each axis-capable
        argument through :func:`parse_axis_spec`.
        """
        axes = {}
        fixed = {}
        for key, values in params.items():
            if not values:
                continue
            if len(values) == 1:
                fixed[key] = values[0]
            else:
                axes[key] = list(values)
        return cls(axes=axes, fixed=fixed, name=name, conf=conf)

    @classmethod
    def from_file(cls, path, cli_overrides=None, cli_conf=None):
        """Load a TOML sweep definition.

        The whole file is the sweep definition — no outer ``[sweep]``
        wrapper.  Schema::

            # Sweep name (optional; auto-generated if omitted).
            name = "fluxion-vs-simple"

            # Parameters at top level.  Scalars are fixed across
            # the sweep; lists and idset/range strings become axes.
            test = "throughput"
            nodes = "16-1024:2"            # axis: RFC 45 range
            njobs = [4096, 8192]           # axis: explicit list
            cores_per_node = 64            # fixed: scalar

            # Optional [[scheduler]] array-of-tables: each entry
            # contributes one value to the implicit ``scheduler``
            # axis.  The block's ``name`` is the scheduler axis
            # value used in records and the dashboard.  Additional
            # fields (modules, env) describe how to load the
            # scheduler in the child instance.
            [[scheduler]]
            name = "simple"
            modules = [{ name = "sched-simple" }]

            [[scheduler]]
            name = "fluxion"
            modules = [
              { name = "sched-fluxion-resource", options = "policy=high" },
              { name = "sched-fluxion-qmanager", options = "..." },
            ]
            env = { FLUX_MODULE_PATH = "/path/to/sched/modules" }

            # Conf entries forwarded to every child run as
            # --conf=KEY=VALUE.  Same semantics as the CLI --conf.
            [conf]
            "resource.noverify" = true

        ``cli_overrides`` is a dict mapping parameter name to a
        list of values that REPLACE the file's value for that
        parameter (CLI wins).  ``cli_conf`` is a list of additional
        ``--conf`` entries appended to the file's ``[conf]``.
        """
        with open(path, "rb") as f:
            doc = tomllib.load(f)

        # Pull out the structured sections; everything else at
        # top level is a parameter.
        name = doc.pop("name", None)
        conf_section = doc.pop("conf", {})
        scheduler_blocks = doc.pop("scheduler", None)

        axes = {}
        fixed = {}
        for key, raw in doc.items():
            values = _coerce_param_value(raw)
            if len(values) == 1:
                fixed[key] = values[0]
            else:
                axes[key] = values

        # [[scheduler]] entries become the scheduler axis and
        # populate ``scheduler_recipes``.  Each block's ``name``
        # is the axis value (and dashboard/record label); the
        # block's ``modules`` / ``conf`` / ``env`` fields are
        # consulted at dispatch time to expand into child
        # ``--scheduler=...`` / ``--conf=...`` args plus a
        # per-run environment overlay.
        scheduler_recipes = {}
        if scheduler_blocks:
            sched_names = []
            for block in scheduler_blocks:
                recipe = _recipe_from_block(block)
                if recipe["name"] in scheduler_recipes:
                    raise ValueError(
                        f"{path}: duplicate [[scheduler]] name " f"{recipe['name']!r}"
                    )
                scheduler_recipes[recipe["name"]] = recipe
                sched_names.append(recipe["name"])
            if "scheduler" in axes or "scheduler" in fixed:
                raise ValueError(
                    f"{path}: scheduler given both as a plain "
                    f"parameter and as [[scheduler]] blocks; "
                    f"pick one form"
                )
            if len(sched_names) == 1:
                fixed["scheduler"] = sched_names[0]
            else:
                axes["scheduler"] = sched_names

        # CLI overrides replace file values wholesale.  Single
        # values land in fixed; lists land in axes.
        for k, values in (cli_overrides or {}).items():
            # Remove any existing entry in the other bucket first.
            axes.pop(k, None)
            fixed.pop(k, None)
            if len(values) == 1:
                fixed[k] = values[0]
            else:
                axes[k] = list(values)

        # Conf entries: file's [conf] dict flattened into the
        # KEY=VALUE string form the CLI takes.
        conf = []
        for k, v in conf_section.items():
            conf.append(f"{k}={_format_conf_value(v)}")
        conf.extend(cli_conf or [])

        return cls(
            axes=axes,
            fixed=fixed,
            name=name,
            conf=conf,
            scheduler_recipes=scheduler_recipes,
        )

    def expand(self):
        """Enumerate :class:`RunSpec` instances over the full
        cross-product of axes, in declaration order (outer-to-inner
        per the dict insertion order).  Yields lazily.

        Computes per-axis column widths (the longest formatted value
        for each varying axis) and threads them into each RunSpec
        so the dashboard's per-row axis summary renders in proper
        columns, not just space-separated.

        Resolves the per-run scheduler recipe via
        :meth:`_resolve_recipe`: each run's ``scheduler`` axis
        value (or the fixed ``scheduler``) is looked up in
        ``scheduler_recipes`` from a TOML ``[[scheduler]]`` block;
        if absent, a simple single-module recipe is synthesized
        from the name and any ``scheduler_options``.  The recipe
        rides on each :class:`RunSpec` so :meth:`RunSpec.argv` can
        emit recipe-driven scheduler args without a back-reference
        to the matrix.
        """
        if not self.axes:
            raise ValueError("sweep has no axes; nothing to expand")
        keys = list(self.axes.keys())
        value_lists = [self.axes[k] for k in keys]
        total = 1
        for vl in value_lists:
            if not vl:
                raise ValueError(f"axis {keys[value_lists.index(vl)]!r} has no values")
            total *= len(vl)
        varying = self.varying_axes()
        col_widths = _per_axis_column_widths(self.axes, varying)
        for i, combo in enumerate(itertools.product(*value_lists)):
            axes = dict(zip(keys, combo))
            # Scheduler can come from axis or fixed; scheduler_options
            # likewise.  Axis values shadow fixed values (the standard
            # precedence used by argv()).
            sched_name = axes.get("scheduler") or self.fixed.get("scheduler")
            sched_opts = axes.get("scheduler_options") or self.fixed.get(
                "scheduler_options"
            )
            recipe = self._resolve_recipe(sched_name, sched_opts) if sched_name else {}
            yield RunSpec(
                axes=axes,
                fixed=self.fixed,
                run_index=i,
                total=total,
                conf=list(self.conf),
                varying_axes=varying,
                axis_column_widths=col_widths,
                scheduler_recipe=recipe,
            )

    def axes_summary(self):
        """One-line description of the sweep's varying axes, for the
        dashboard title.  Axes with a single value are
        effectively-fixed and contribute nothing to comparing one
        run against another, so they're omitted here to keep the
        title line short enough to fit on narrow terminals."""
        bits = []
        for k, values in self.axes.items():
            if len(values) <= 1:
                continue
            if len(values) <= 4:
                bits.append(f"{k}={{{','.join(str(v) for v in values)}}}")
            else:
                bits.append(f"{k}={{{values[0]}…{values[-1]}, " f"n={len(values)}}}")
        return " × ".join(bits) if bits else "(single configuration)"

    def varying_axes(self):
        """Set of axis names that have more than one value.  Passed
        to :class:`RunSpec` instances by :meth:`expand` so the
        dashboard's per-row axis summaries only show the axes that
        actually distinguish one row from another."""
        return {k for k, vl in self.axes.items() if len(vl) > 1}


def _format_axis_value(k, v):
    """Render one axis value the way :meth:`RunSpec.axis_summary`
    will display it: bare for ``scheduler`` (no ``scheduler=`` and
    no ``sched-`` prefix), ``key=value`` for everything else.

    Used both by the per-row renderer and by
    :func:`_per_axis_column_widths` to compute consistent column
    widths from a matrix's full value lists.
    """
    if k == "scheduler" and isinstance(v, str):
        return v.replace("sched-", "")
    return f"{k}={v}"


def _per_axis_column_widths(axes, varying):
    """Compute the max formatted-value width for each varying axis,
    given the matrix's full ``axes`` (axis name -> list of values).
    Stored on each :class:`RunSpec` so per-row rendering can pad to
    matching column widths."""
    widths = {}
    for k in varying:
        widths[k] = max(
            (len(_format_axis_value(k, v)) for v in axes[k]),
            default=0,
        )
    return widths


def _default_sweep_name():
    """Generate a sweep name from the current timestamp.  Users
    can override via ``--sweep-name`` or the file's ``name`` key."""
    return f"sweep-{time.strftime('%Y%m%dT%H%M%S')}"


def generate_sweep_id():
    """Globally-unique id for one sweep invocation.  Used as the
    primary key for cross-referencing runs in the results file."""
    return (
        f"sweep-{time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}"
        f"-{random.getrandbits(32):08x}"
    )


# ===========================================================
# UI layer
# ===========================================================

# ANSI constants, glyph sets, and terminal helpers are shared with ui.py
# via flux.testing.schedbench._ansi (imported at the top of this module).

#: Layout knobs.  Mini progress bars in active rows are short — the
#: row already carries identity + stage + count, so the bar is
#: glanceable rather than the headline.
_MINI_BAR_WIDTH = 11
#: Match any ANSI CSI escape sequence (the only kind we emit:
#: colors, cursor moves, line erase, DEC private modes).  Used by
#: :func:`_truncate_visible` to ignore zero-width control bytes
#: when measuring or trimming a line.
_ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def _truncate_visible(s, width):
    """Truncate ``s`` so its visible (escape-free) length is at most
    ``width``, leaving any in-flight ANSI styling untouched.

    Designed to prevent terminal line-wrap in the live dashboard.
    A wrapped logical line becomes two visual rows, which throws off
    the cursor-up bookkeeping used to overwrite previous frames and
    causes them to stack vertically.

    Naive single-pass: walk characters, preserve any ANSI escape we
    encounter, count only non-escape characters against the budget.
    Append a final ``\\x1b[0m`` reset if we may have truncated
    inside an active styled span (the closing ``_RESET`` of that
    span would otherwise be lost).
    """
    if not s:
        return s
    if len(_ANSI_RE.sub("", s)) <= width:
        return s
    out = []
    visible = 0
    saw_escape = False
    i = 0
    n = len(s)
    while i < n and visible < width:
        if s[i] == "\x1b":
            m = _ANSI_RE.match(s, i)
            if m:
                out.append(m.group(0))
                saw_escape = True
                i = m.end()
                continue
        out.append(s[i])
        visible += 1
        i += 1
    if saw_escape:
        out.append(_RESET)
    return "".join(out)


def _fmt_elapsed(seconds):
    """Compact mm:ss style for the active/elapsed columns."""
    seconds = max(0.0, seconds)
    m, s = divmod(int(seconds), 60)
    return f"{m:d}m {s:02d}s"


#: Maximum number of run rows visible in the unified list at once.
#: For sweeps with more runs than this, the list windows around the
#: active region with elision indicators above and below.  Chosen
#: to leave room on a typical 24-row terminal for the title block,
#: aggregate bar, and footer while still showing enough runs for
#: context.
_MAX_VISIBLE_RUNS = 12


class _RunState:
    """Per-run state for the unified-list dashboard.

    Every run in the sweep gets exactly one ``_RunState`` instance,
    created at sweep_start time with ``status="pending"`` and
    updated in place as the run transitions through the lifecycle
    (active -> done or failed).  The dashboard's per-row rendering
    reads this state to choose glyph, color, and trailing content
    — but the *row itself* never moves once assigned, which is the
    whole point of the design.
    """

    __slots__ = (
        "run_index",
        "test_name",
        "axes",
        "status",
        "stage",
        "current",
        "total",
        "started_at",
        "completed_at",
        "metric_display",
        "error_message",
    )

    def __init__(self, run_index, test_name, axes):
        self.run_index = run_index
        self.test_name = test_name
        self.axes = dict(axes)
        self.status = "pending"
        self.stage = ""
        self.current = 0
        self.total = 0
        self.started_at = None
        self.completed_at = None
        self.metric_display = ""
        self.error_message = ""


class TerminalSweepEmitter:
    """Render sweep progress as a calm, professional dashboard.

    The dashboard is a unified, ordered list of all runs in the
    sweep — one row per run, glyph and trailing content evolving
    in place as the run transitions pending -> active -> done (or
    failed).  No section migrations, no row reordering, no
    "completed" pane that shifts when something finishes; a run's
    row is its row for the whole sweep.

    For sweeps too large to fit on screen, the list windows around
    the active region with elision indicators ("+ N earlier" /
    "+ N later") at the boundaries.  Slides are infrequent —
    usually one row at a time as the active frontier advances —
    rather than every-transition churn.

    Constructor args:
        stream: output stream (default sys.stdout)
        color: ``"auto"`` / ``"always"`` / ``"never"``
        ascii_only: force ASCII glyphs and bar characters; auto-
            detected from locale if omitted
    """

    def __init__(self, stream=None, color="auto", ascii_only=None):
        self._stream = stream if stream is not None else sys.stdout
        self._color = _color_supported(self._stream, color)
        self._ascii = ascii_only if ascii_only is not None else not _safe_glyphs()
        self._is_tty = _isatty(self._stream)
        self._glyphs = _GLYPHS_ASCII if self._ascii else _GLYPHS_UNICODE
        self._bar_chars = _BAR_CHARS_ASCII if self._ascii else _BAR_CHARS_UNICODE
        self._sep = "-" if self._ascii else "·"

        # Sweep-level state.
        self._sweep_name = None
        self._axes_summary = ""
        self._total = 0
        self._headline_metric_key = None
        self._headline_metric_unit = ""

        # Per-run state — the entire sweep's worth, populated at
        # sweep_start.  Indexed by run_index; each entry is one
        # _RunState that lives in its assigned row for the whole
        # sweep.
        self._all_runs = []

        # Column layout for the tabular run list.  Set at
        # sweep_start by :meth:`_compute_columns`.  Each entry is a
        # dict: ``{name, header, width, align, getter}`` where
        # ``getter(rs)`` returns the cell string for the given run.
        # The STATUS column is special: it has no fixed width and
        # composes its content dynamically based on rs.status.
        self._columns = []

        # Rendering state.
        self._start_time = None
        self._end_time = None
        self._last_render_lines = 0
        self._last_render_t = 0.0
        self._cursor_hidden = False

    # ----- Public surface (called by SweepRunner) -----

    def sweep_start(
        self,
        name,
        total,
        axes_summary,
        headline_metric=None,
        axis_col_width=None,
        max_active_visible=None,
        run_summaries=None,
    ):
        """Initialize the dashboard for a new sweep.

        Args:
            name: human-readable sweep name (from --sweep-name or
                generated default)
            total: number of runs in the expansion
            axes_summary: one-line description of axes (from
                :meth:`SweepMatrix.axes_summary`)
            headline_metric: ``(key, label, unit)`` triple naming
                the per-row result metric.
            axis_col_width: unused in the unified design (axis
                columns size themselves per-axis).  Accepted for
                signature back-compat.
            max_active_visible: unused — the unified design shows
                all active rows in their permanent positions,
                bounded only by the visible window.  Accepted for
                signature back-compat.
            run_summaries: sequence of ``(test_name, axis_summary,
                per_axis_values)`` tuples describing every run in
                the sweep, in expansion order.  ``per_axis_values``
                is the run's ``RunSpec.axes`` dict.  When supplied,
                the dashboard pre-allocates a row per run and
                computes accurate per-axis column widths up front.
                If omitted, rows are added lazily as ``run_started``
                fires (less ideal — the layout shifts the first
                few times because column widths weren't known).
        """
        self._sweep_name = name
        self._total = total
        self._axes_summary = axes_summary
        if headline_metric is not None:
            key, _label, unit = headline_metric
            self._headline_metric_key = key
            self._headline_metric_unit = unit

        # Pre-populate the run list from the dispatcher's manifest.
        # Each tuple: (test_name, axes_dict).  axis_summary strings
        # are no longer needed — the tabular renderer reads values
        # directly from the axes dict per-column.
        if run_summaries is not None:
            self._all_runs = [
                _RunState(i, test_name, axes_dict)
                for i, (test_name, axes_dict) in enumerate(run_summaries)
            ]
            self._compute_columns(run_summaries)
        else:
            self._all_runs = []
            self._columns = []

        self._start_time = time.time()
        if self._is_tty:
            self._stream.write(_CURSOR_HIDE)
            self._cursor_hidden = True
        self._redraw(force=True)

    def run_started(self, run_index, axis_summary, test_name):
        # axis_summary is unused by the tabular renderer (it reads
        # axes per-column from the pre-populated run state) but
        # kept in the signature for back-compat with dispatchers
        # that still pass it.
        del axis_summary
        rs = self._get_or_create_run(run_index, test_name)
        rs.status = "active"
        rs.started_at = time.time()
        rs.stage = ""
        rs.current = 0
        rs.total = 0
        self._redraw()

    def run_event(self, run_index, event_name, ctx):
        """Process a stage/progress event for an active run."""
        if run_index >= len(self._all_runs):
            return
        rs = self._all_runs[run_index]
        if rs.status != "active":
            return
        if event_name == "stage":
            rs.stage = ctx.get("stage", "")
            rs.current = 0
            rs.total = 0
        elif event_name == "progress":
            rs.current = ctx.get("current", 0)
            rs.total = ctx.get("total", 0)
        self._redraw()

    def run_completed(self, run_index, metrics):
        if run_index >= len(self._all_runs):
            return
        rs = self._all_runs[run_index]
        rs.status = "done"
        rs.completed_at = time.time()
        if self._headline_metric_key:
            rs.metric_display = self._format_metric_value(
                metrics.get(self._headline_metric_key)
            )
        self._redraw(force=True)

    def run_failed(self, run_index, error):
        if run_index >= len(self._all_runs):
            return
        rs = self._all_runs[run_index]
        rs.status = "failed"
        rs.completed_at = time.time()
        rs.error_message = str(error).splitlines()[0] if error else "unknown error"
        self._redraw(force=True)

    def sweep_complete(self):
        self._end_time = time.time()
        self._redraw(force=True, final=True)
        self._show_cursor()

    # ----- Column / layout helpers -----

    def _get_or_create_run(self, run_index, test_name):
        """Lazy fallback when sweep_start didn't pre-populate the
        run list.  Extends ``_all_runs`` to fit ``run_index``."""
        while run_index >= len(self._all_runs):
            i = len(self._all_runs)
            self._all_runs.append(_RunState(i, "", {}))
        rs = self._all_runs[run_index]
        if not rs.test_name:
            rs.test_name = test_name
        return rs

    def _compute_columns(self, run_summaries):
        """Build the column layout from the full run manifest.

        Columns:

        * ``BENCHMARK`` — always present.  Width = max test-name
          width across the sweep, padded to the header.
        * One column per *varying* axis (axes with more than one
          value across the sweep).  Effectively-fixed axes are
          omitted since they'd be the same in every row.  Numeric
          axes right-align; string axes left-align.  Column header
          is the axis name uppercased.
        * ``STATUS`` — the dynamic column.  No fixed width; its
          content composes the lifecycle info (progress bar +
          counts for active runs, the result for done runs, the
          error for failed runs, nothing for pending).

        Each column is a dict with the fields ``name``, ``header``,
        ``width``, ``align``, and ``getter``.  ``getter`` is a
        function that receives a ``_RunState`` and returns the cell
        string for that row.  STATUS uses ``getter=None`` since its
        content is composed dynamically by :meth:`_render_status_cell`.
        """
        # BENCHMARK column.
        test_names = {t for t, _x in run_summaries}
        test_width = max(
            (len(t) for t in test_names),
            default=0,
        )
        bench_width = max(test_width, len("BENCHMARK"))
        self._columns = [
            {
                "name": "test",
                "header": "BENCHMARK",
                "width": bench_width,
                "align": "left",
                "getter": lambda rs: rs.test_name,
            }
        ]

        # Discover varying axes from the manifest.  Preserve the
        # axes' declaration order (first run's dict insertion
        # order); that matches the CLI flag order the user typed.
        if run_summaries:
            axis_order = list(run_summaries[0][1].keys())
        else:
            axis_order = []
        axis_value_sets = {k: set() for k in axis_order}
        for _t, axes_dict in run_summaries:
            for k, v in axes_dict.items():
                axis_value_sets.setdefault(k, set()).add(v)
                if k not in axis_order:
                    axis_order.append(k)

        for axis_name in axis_order:
            values = axis_value_sets[axis_name]
            if len(values) <= 1:
                # Effectively-fixed across the sweep — no column.
                continue
            header = axis_name.upper()
            value_widths = max(len(str(v)) for v in values)
            width = max(value_widths, len(header))
            is_numeric = all(
                isinstance(v, (int, float)) and not isinstance(v, bool) for v in values
            )
            align = "right" if is_numeric else "left"
            # Bind axis_name into the lambda's default arg to avoid
            # late-binding closure pitfalls.
            self._columns.append(
                {
                    "name": axis_name,
                    "header": header,
                    "width": width,
                    "align": align,
                    "getter": lambda rs, k=axis_name: str(rs.axes.get(k, "")),
                }
            )

        # STATUS column — no fixed width; renders dynamically.
        self._columns.append(
            {
                "name": "status",
                "header": "STATUS",
                "width": 0,
                "align": "left",
                "getter": None,
            }
        )

    # ----- Rendering -----

    def _show_cursor(self):
        if self._cursor_hidden and self._is_tty:
            self._stream.write(_CURSOR_SHOW)
            self._stream.flush()
            self._cursor_hidden = False

    def _redraw(self, force=False, final=False):
        now = time.time()
        if not force and (now - self._last_render_t) < _REDRAW_INTERVAL_S:
            return
        self._last_render_t = now

        lines = self._render_lines()
        new_count = len(lines)
        width = self._term_width()

        # Pad shrinking frames so trailing lines from the prior
        # frame get scrubbed by _ERASE_TO_EOL.
        emitted_count = max(new_count, self._last_render_lines)
        if new_count < emitted_count:
            lines = lines + [""] * (emitted_count - new_count)

        # Truncate each line to the terminal width so it never
        # wraps to a second visual row.  Logical-line wrap would
        # make our cursor-up count (in logical lines) drift below
        # what the cursor actually needs to traverse (visual lines),
        # producing the frame-stacking glitch on narrow terminals.
        # Leave one column of slack for terminals that consider a
        # cursor at column == width to be on the next line already.
        lines = [_truncate_visible(line, width - 1) for line in lines]

        parts = []
        if self._is_tty:
            parts.append(_SYNC_BEGIN)
            if self._last_render_lines > 0:
                parts.append(_CURSOR_UP_FMT.format(self._last_render_lines))
        parts.append("\n".join(line + _ERASE_TO_EOL for line in lines))
        parts.append("\n")
        if self._is_tty:
            parts.append(_SYNC_END)

        self._stream.write("".join(parts))
        self._stream.flush()
        # Track EMITTED count (including padding) — next frame's
        # cursor-up must rewind over everything we actually wrote,
        # not just the logical-content count.
        self._last_render_lines = emitted_count

    def _c(self, code, text):
        return code + text + _RESET if self._color else text

    def _term_width(self):
        try:
            return max(40, os.get_terminal_size(self._stream.fileno()).columns)
        except (AttributeError, ValueError, OSError):
            return 100

    def _render_lines(self):
        out = []
        width = self._term_width()
        sep = self._sep

        # Header.
        title = (
            self._c(_BOLD, "flux schedbench sweep")
            + " "
            + sep
            + " "
            + (self._sweep_name or "")
        )
        out.append(title)
        if self._axes_summary:
            out.append(self._c(_DIM, f"  {self._axes_summary}"))
        out.append("")

        # Aggregate progress bar with inline stats.
        out.append("  " + self._render_aggregate_bar(width))
        out.append("")

        # Column header row.
        out.append(self._render_header_row())

        # Unified run list.  Window around the active region for
        # large sweeps; show all rows otherwise.
        ellipsis = "..." if self._ascii else "⋯"
        visible, n_above, n_below = self._select_visible_runs()
        if n_above > 0:
            out.append(
                self._c(
                    _DIM,
                    f"   {ellipsis}  {n_above} earlier",
                )
            )
        for rs in visible:
            out.append(self._render_run_row(rs))
        if n_below > 0:
            out.append(
                self._c(
                    _DIM,
                    f"   {ellipsis}  {n_below} more pending",
                )
            )

        # Footer when complete.
        if self._end_time is not None:
            out.append("")
            out.append(self._render_final_footer())

        return out

    def _render_header_row(self):
        """Render the column header line in dim bold.

        Layout matches :meth:`_render_run_row` exactly so columns
        line up: glyph-column placeholder (3 chars), then each
        column header padded to its width with 2-char separators.
        """
        parts = ["    "]  # placeholder for the glyph column
        for col in self._columns:
            if col["name"] == "status":
                header_text = col["header"]
            elif col["align"] == "right":
                header_text = col["header"].rjust(col["width"])
            else:
                header_text = col["header"].ljust(col["width"])
            parts.append(self._c(_BOLD, self._c(_DIM, header_text)))
            parts.append("  ")
        return "".join(parts).rstrip()

    def _render_run_row(self, rs):
        """Render one run's tabular row.  The glyph column carries
        the lifecycle state (color-coded); the data columns hold
        per-axis values; the STATUS column composes lifecycle
        content dynamically."""
        parts = [f"  {self._glyph_for(rs.status)} "]
        for col in self._columns:
            if col["name"] == "status":
                parts.append(self._render_status_cell(rs))
            else:
                value = col["getter"](rs)
                if col["align"] == "right":
                    parts.append(value.rjust(col["width"]))
                else:
                    parts.append(value.ljust(col["width"]))
            parts.append("  ")
        return "".join(parts).rstrip()

    def _render_status_cell(self, rs):
        """Compose the STATUS column's content based on the run's
        lifecycle state.  Pending = empty (visual quiet); active =
        stage + mini bar + count + elapsed; done = the result;
        failed = the error message."""
        if rs.status == "active":
            stage_col = (rs.stage or "").ljust(8)
            bar = self._render_mini_bar(rs.current, rs.total)
            count = (f"{rs.current}/{rs.total}" if rs.total > 0 else "").rjust(9)
            elapsed_s = time.time() - rs.started_at if rs.started_at else 0
            elapsed = _fmt_elapsed(elapsed_s)
            return f"{stage_col}  {bar}  {count}  {elapsed}"
        if rs.status == "done":
            return self._c(_FG_GREEN, rs.metric_display)
        if rs.status == "failed":
            return self._c(_FG_RED, f"error: {rs.error_message}")
        return ""  # pending

    def _select_visible_runs(self):
        """Choose which subset of ``_all_runs`` to display.

        Returns ``(visible_runs, n_elided_above, n_elided_below)``.

        For sweeps with at most ``_MAX_VISIBLE_RUNS`` runs, returns
        everything.  For larger sweeps, picks a contiguous window
        with one critical constraint: **every active row must be
        visible**.  This matters for parallel dispatch — without
        it, a slow earlier run anchors the window on itself and
        later-but-actively-running siblings disappear into the
        bottom elision (the "more pending" group), even though
        they're not pending at all.

        When active rows fit within the budget, the window adds
        context biased ~2/3 below / 1/3 above so the user sees
        forward motion.  When the active span itself exceeds the
        budget (a slow earlier run plus active later ones, with
        many intervening completed rows), the window grows to
        cover them all — exceeding _MAX_VISIBLE_RUNS in that
        uncommon case, which is the right tradeoff: showing all
        in-progress work beats clamping to a fixed row count.
        """
        n = len(self._all_runs)
        if n <= _MAX_VISIBLE_RUNS:
            return self._all_runs, 0, 0

        active_indices = [
            i for i, rs in enumerate(self._all_runs) if rs.status == "active"
        ]

        if active_indices:
            first_active = active_indices[0]
            last_active = active_indices[-1]
            span = last_active - first_active + 1
            if span >= _MAX_VISIBLE_RUNS:
                # Active region alone fills the budget.  Show
                # exactly that span — completed rows interleaved
                # between active ones come along for the ride.
                start = first_active
                end = last_active + 1
            else:
                # Active rows fit; remaining slots are context.
                # 1/3 above the first active row, 2/3 below the
                # last — biased toward upcoming work.
                budget = _MAX_VISIBLE_RUNS - span
                before = min(first_active, budget // 3)
                start = max(0, first_active - before)
                end = min(n, start + _MAX_VISIBLE_RUNS)
                if end - start < _MAX_VISIBLE_RUNS:
                    # Near the end of the matrix; pull start
                    # back so the window stays full.
                    start = max(0, end - _MAX_VISIBLE_RUNS)
            return self._all_runs[start:end], start, n - end

        # No active runs (between submissions or post-sweep):
        # anchor on the next pending row, or the end if all done.
        anchor = None
        for i, rs in enumerate(self._all_runs):
            if rs.status == "pending":
                anchor = i
                break
        if anchor is None:
            anchor = n - 1
        anchor_pos_in_window = _MAX_VISIBLE_RUNS // 3
        start = max(0, anchor - anchor_pos_in_window)
        end = min(n, start + _MAX_VISIBLE_RUNS)
        start = max(0, end - _MAX_VISIBLE_RUNS)
        return self._all_runs[start:end], start, n - end

    def _glyph_for(self, status):
        """Glyph + color for a run's lifecycle state.  Pending is
        dim (de-emphasized), active is bright cyan (the eye's focus
        target during a sweep), done is green, failed is red."""
        g = self._glyphs
        if status == "active":
            return self._c(_FG_CYAN, g["active"])
        if status == "done":
            return self._c(_FG_GREEN, g["done"])
        if status == "failed":
            return self._c(_FG_RED, g["failed"])
        # pending
        return self._c(_DIM, g["pending"])

    def _render_aggregate_bar(self, width):
        """Aggregate bar with inline counts and elapsed time.
        Single line at the top of the dashboard — the at-a-glance
        "how is the sweep going" indicator."""
        fill, empty = self._bar_chars
        n_done = sum(1 for rs in self._all_runs if rs.status == "done")
        n_failed = sum(1 for rs in self._all_runs if rs.status == "failed")
        completed = n_done + n_failed
        total = max(1, self._total)
        bar_width = max(20, min(width - 50, 50))
        ratio = completed / total
        filled = int(round(ratio * bar_width))
        bar_str = fill * filled + empty * (bar_width - filled)
        pct = int(ratio * 100)
        color = (
            _FG_GREEN
            if completed == self._total and n_failed == 0
            else (_FG_RED if n_failed else _FG_CYAN)
        )
        # Compose the inline stats: "X/N (P%)  elapsed  failed-if-any"
        stats = f"  {completed}/{self._total} ({pct}%)"
        if self._start_time is not None:
            elapsed = (
                self._end_time if self._end_time else time.time()
            ) - self._start_time
            stats += "  " + self._c(_DIM, _fmt_elapsed(elapsed))
        if n_failed > 0:
            stats += "  " + self._c(
                _FG_RED, f"{self._glyphs['failed']} {n_failed} failed"
            )
        return self._c(color, bar_str) + stats

    def _render_mini_bar(self, current, total):
        fill, empty = self._bar_chars
        if total <= 0:
            return self._c(_DIM, empty * _MINI_BAR_WIDTH)
        ratio = min(1.0, current / total)
        filled = int(round(ratio * _MINI_BAR_WIDTH))
        bar = fill * filled + empty * (_MINI_BAR_WIDTH - filled)
        return self._c(_FG_CYAN, bar)

    def _format_metric_value(self, value):
        """Format the per-row headline metric for the dashboard.

        Renders as ``result=N [unit]`` regardless of which underlying
        metric the benchmark designates as its headline.  Uniform
        display lets the user scan across rows (and across mixed-
        test sweeps) without tracking which column means what.

        Uses integer-with-commas for any magnitude ≥ 1, three
        decimals for sub-1 ratios, scientific notation below
        0.001.
        """
        if value is None:
            return ""
        try:
            v = float(value)
        except (TypeError, ValueError):
            return f"result={value}"
        av = abs(v)
        if av >= 1:
            num = f"{v:,.0f}"
        elif av >= 0.001:
            num = f"{v:.3f}"
        else:
            num = f"{v:.2e}"
        unit = self._headline_metric_unit
        if unit:
            return f"result={num} {unit}"
        return f"result={num}"

    def _counts(self):
        """Return ``(n_done, n_failed, n_active, n_pending)`` from
        the unified run list."""
        n_done = 0
        n_failed = 0
        n_active = 0
        for rs in self._all_runs:
            if rs.status == "done":
                n_done += 1
            elif rs.status == "failed":
                n_failed += 1
            elif rs.status == "active":
                n_active += 1
        n_pending = self._total - n_done - n_failed - n_active
        return n_done, n_failed, n_active, max(0, n_pending)

    def _render_final_footer(self):
        n_done, n_failed, _n_active, _n_pending = self._counts()
        completed = n_done + n_failed
        elapsed = (self._end_time or time.time()) - (self._start_time or 0)
        line = (
            f"  {completed}/{self._total} complete · "
            f"{n_failed} failed · {_fmt_elapsed(elapsed)} elapsed"
        )
        return self._c(_BOLD, line)


class LineSweepEmitter:
    """Plain-text sweep emitter for non-TTY output.

    One line per significant state transition: sweep start, run
    start, run completion (with headline metric), run failure
    (with brief error), and a closing summary line.  Per-run
    progress events from child runs are dropped — line-per-run is
    the level of detail this mode aims for.

    Sweep is the top-level tool, so we don't bother emitting a
    structured event stream from it: there is nothing above sweep
    that would consume one.  Anything that wants the full event
    stream of an individual benchmark should run ``flux schedbench
    run`` directly with ``--ui=off``.
    """

    def __init__(self, stream=None):
        self._stream = stream if stream is not None else sys.stderr
        self._total = 0
        self._sweep_name = None
        self._headline_metric_key = None
        self._headline_metric_unit = ""
        self._n_ok = 0
        self._n_failed = 0
        self._start_time = None

    def _line(self, text):
        print(text, file=self._stream, flush=True)

    # --- Public surface (matches TerminalSweepEmitter) ---

    def sweep_start(
        self,
        name,
        total,
        axes_summary,
        headline_metric=None,
        axis_col_width=None,
        max_active_visible=None,
        run_summaries=None,
    ):
        # axis_col_width, max_active_visible, and run_summaries
        # are dashboard layout concerns; the line emitter has none
        # of those.  Accepted for interface parity with
        # TerminalSweepEmitter so the dispatcher can call either
        # without branching.
        self._sweep_name = name
        self._total = total
        if headline_metric:
            self._headline_metric_key = headline_metric[0]
            self._headline_metric_unit = headline_metric[2]
        self._start_time = time.time()
        self._line(f"sweep {name}: {total} runs · {axes_summary}")

    def run_started(self, run_index, axis_summary, test_name):
        self._line(
            f"  [{run_index + 1:>{len(str(self._total))}}/"
            f"{self._total}] start  {test_name}  {axis_summary}"
        )

    def run_event(self, run_index, event_name, ctx):
        pass  # per-child progress is not surfaced in line mode

    def run_completed(self, run_index, metrics):
        self._n_ok += 1
        metric_str = self._format_metric(metrics)
        self._line(
            f"  [{run_index + 1:>{len(str(self._total))}}/"
            f"{self._total}] ok     {metric_str}"
        )

    def run_failed(self, run_index, error):
        self._n_failed += 1
        msg = str(error).splitlines()[0] if error else "unknown"
        self._line(
            f"  [{run_index + 1:>{len(str(self._total))}}/"
            f"{self._total}] FAIL   error: {msg}"
        )

    def sweep_complete(self):
        elapsed = time.time() - (self._start_time or time.time())
        self._line(
            f"sweep {self._sweep_name}: {self._n_ok} ok, "
            f"{self._n_failed} failed, {elapsed:.1f}s elapsed"
        )

    def _format_metric(self, metrics):
        """See :meth:`TerminalSweepEmitter._format_metric_value` for
        the rationale.  Renders as ``result=N [unit]`` so the line
        log reads uniformly across benchmarks."""
        if not self._headline_metric_key or not metrics:
            return ""
        v = metrics.get(self._headline_metric_key)
        if v is None:
            return ""
        try:
            f = float(v)
        except (TypeError, ValueError):
            return f"result={v}"
        av = abs(f)
        if av >= 1:
            num = f"{f:,.0f}"
        elif av >= 0.001:
            num = f"{f:.3f}"
        else:
            num = f"{f:.2e}"
        if self._headline_metric_unit:
            return f"result={num} {self._headline_metric_unit}"
        return f"result={num}"


# ===========================================================
# Dispatch layer
# ===========================================================


def _pick_headline_metric(bench_cls):
    """Choose the per-row headline metric for the sweep dashboard.

    Prefers an explicit ``RESULT`` declaration on the benchmark
    class — a ``(key, unit)`` tuple naming the canonical outcome.
    Example declarations:

    .. code-block:: python

        class Throughput(Benchmark):
            RESULT = ("throughput", "job/s")

        class FillMachine(Benchmark):
            RESULT = ("alloc_rate", "job/s")

        class Locality(Benchmark):
            RESULT = ("mean_locality_score", "")    # dimensionless

    The sweep renders all benchmarks uniformly as ``result=N unit``
    so a mixed-test sweep can be scanned in one glance — the user
    doesn't have to track which column shows what.

    Falls back to a heuristic pick from ``SUMMARY_METRICS`` (first
    entry whose unit isn't ``"count"``) for benchmarks that haven't
    declared ``RESULT`` yet.  Returns ``(key, label, unit)`` or
    ``None``.
    """
    result = getattr(bench_cls, "RESULT", None)
    if result is not None:
        key = result[0]
        unit = result[1] if len(result) > 1 else ""
        # Synthesize a (key, label, unit) triple so the rest of
        # the dashboard code can treat RESULT and SUMMARY_METRICS
        # interchangeably.  The label is unused by the sweep
        # display itself (we render as "result=N") but is kept
        # populated for consistency with the existing shape.
        return (key, key, unit)
    metrics = getattr(bench_cls, "SUMMARY_METRICS", None) or []
    if not metrics:
        return None
    for entry in metrics:
        if len(entry) >= 3 and entry[2] != "count":
            return entry
    return metrics[0]


def _prep_emitter_sweep_start(matrix, runs, emitter, max_active_visible):
    """Common sweep_start setup shared by serial and Flux dispatch.

    Computes the headline metric and identity column width from the
    expanded run set and fires :meth:`sweep_start` on the emitter
    with a full manifest so it can pre-allocate rows + columns.
    """
    bench_cls = BENCHMARKS.get(runs[0].test_name())
    headline = _pick_headline_metric(bench_cls)
    axis_col_width = min(
        max(len(f"{r.test_name()}  {r.axis_summary()}") for r in runs),
        60,
    )
    emitter.sweep_start(
        name=matrix.name,
        total=len(runs),
        axes_summary=matrix.axes_summary(),
        headline_metric=headline,
        axis_col_width=axis_col_width,
        max_active_visible=max_active_visible,
        run_summaries=[(r.test_name(), r.axes) for r in runs],
    )


def run_flux(
    matrix, sweep_id, results_file, emitter, flux_schedbench_cmd=None, per_run_nodes=1
):
    """Run a sweep by submitting each run as a Flux job.

    The outer Flux instance handles scheduling: all jobs are
    submitted up front and Flux runs them in parallel up to
    whatever resources are available.  No sweep-level concurrency
    cap — that's Flux's job by design.

    Uses a single :class:`flux.job.JobWatcher` for all submitted
    jobs (the class that powers ``flux submit --watch`` and
    ``flux bulksubmit --watch``).  Output is consumed via
    JobWatcher's ``output_callback`` API: the watcher invokes
    ``on_output(jobid, stream, data)`` with raw output chunks,
    which the sweep buffers per (jobid, stream) and parses as
    one JSON event per complete line.  We submit children with
    ``unbuffered=True`` so events surface live in the dashboard
    rather than being held in the child's stdio buffer; the
    consumer-side line buffering compensates for the resulting
    chunk-boundary uncertainty.

    Per-job lifecycle state (running, complete, failed) is read
    from the watcher's internal :class:`JobProgressBar.jobs` dict
    after :meth:`reactor_run` returns — JobWatcher uses this dict
    for its own tracking regardless of whether the progress bar
    itself is displayed.

    Args:
        matrix: :class:`SweepMatrix` (already expanded by the caller)
        sweep_id: unique sweep identifier; embedded in every record
        results_file: path to append records to (None = don't save)
        emitter: dashboard / event sink
        flux_schedbench_cmd: command prefix for each child;
            defaults to ``["flux", "schedbench"]``
        per_run_nodes: nodes to allocate to each child Flux job.
            Defaults to 1 (enough for fake-resources benchmarks).

    Returns ``(n_ok, n_failed)``.
    """
    # Deferred imports — only the Flux dispatch path needs the
    # Python bindings.
    import flux
    import flux.job
    from flux.job import JobWatcher

    if flux_schedbench_cmd is None:
        flux_schedbench_cmd = ["flux", "schedbench"]
    # Raises ConnectionError or similar if no broker — by design:
    # let the real Flux error surface rather than pre-flighting.
    handle = flux.Flux()
    runs = list(matrix.expand())
    _prep_emitter_sweep_start(
        matrix,
        runs,
        emitter,
        max_active_visible=min(len(runs), 8),
    )
    saver = BenchmarkResults(results_file) if results_file else None

    counters = {"n_ok": 0, "n_failed": 0}

    # Per-job state keyed by ``int(jobid)`` for the output callback
    # lookup.  Insertion order is preserved (3.7+ dict ordering),
    # so iterating gives submission order.  The ``jobid`` field
    # carries the original JobID object — needed by
    # ``watcher.add_jobid`` and not derivable from the int key.
    state_by_jobid = {}

    for rs in runs:
        argv = _build_child_argv(rs, flux_schedbench_cmd)
        # Job name shows up in `flux jobs` output for in-flight
        # debugging.  sweep_id alone is ambiguous (every run in
        # the sweep would share it), so include the run index too.
        job_name = f"schedbench-{sweep_id}-{rs.run_index}"
        try:
            jobspec = _build_sweep_jobspec(
                argv,
                per_run_nodes,
                job_name,
                env_rules=(rs.scheduler_recipe or {}).get("env", []),
            )
            jobid = flux.job.submit(handle, jobspec)
        except Exception as exc:  # noqa: BLE001
            err = f"submit failed: {exc}"
            emitter.run_failed(rs.run_index, err)
            counters["n_failed"] += 1
            _save_record(saver, rs, sweep_id, matrix.name, None, None, err)
            continue
        state_by_jobid[int(jobid)] = {
            "rs": rs,
            "jobid": jobid,
            "started_signaled": False,
            "completed_signaled": False,
            "config": None,
            "metrics": None,
            "error": None,
        }

    # If every submit failed, nothing to watch.
    if not state_by_jobid:
        emitter.sweep_complete()
        return counters["n_ok"], counters["n_failed"]

    # Per-(jobid, stream) partial-line buffer.  Required because
    # we submit with unbuffered=True (so events surface live in
    # the dashboard), which means JobWatcher may hand us chunks
    # that span line boundaries.
    output_partial = {}

    def on_output(jobid, stream, data):
        """JobWatcher output callback: invoked with raw chunks of
        child output.  We buffer per (jobid, stream) and dispatch
        one parsed event per complete line.  At EOF, JobWatcher
        invokes us with ``stream=None`` — we flush any remaining
        partial line then.
        """
        state = state_by_jobid.get(int(jobid))
        if state is None:
            return
        if stream is None:
            for stream_name in ("stdout", "stderr"):
                key = (int(jobid), stream_name)
                tail = output_partial.pop(key, "")
                if tail:
                    _route_line(state, tail)
            return
        key = (int(jobid), stream)
        buf = output_partial.get(key, "") + data
        lines = buf.split("\n")
        # Trailing element is the incomplete tail (or "" if data
        # ended on a newline) — stash for the next call.
        output_partial[key] = lines[-1]
        for line in lines[:-1]:
            _route_line(state, line)

    def _route_line(state, line):
        """Process one complete line of child output.

        Fires :meth:`run_started` on the first line seen.  Parses
        the line as a JSON event and hands it to
        :func:`_apply_child_event`, then checks whether a
        ``result`` event was just persisted — if so, fires
        :meth:`run_completed` live so the dashboard row flips
        green as soon as the benchmark finishes (without waiting
        for every other run in the sweep).
        """
        rs = state["rs"]
        if not state["started_signaled"]:
            state["started_signaled"] = True
            emitter.run_started(
                rs.run_index,
                rs.axis_summary(),
                rs.test_name(),
            )
        line = line.strip()
        if not line:
            return
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            # Non-JSON output (shouldn't happen with --ui=off but
            # be defensive — e.g. stderr noise from the child).
            return
        _apply_child_event(event, state, rs.run_index, emitter)
        # Live completion: the `result` event is the signal that
        # the benchmark finished successfully and metrics are
        # available.  Fire immediately so the dashboard updates
        # while the rest of the sweep continues, rather than
        # waiting for the post-reactor finalize loop.
        if state["metrics"] is not None and not state["completed_signaled"]:
            state["completed_signaled"] = True
            emitter.run_completed(rs.run_index, state["metrics"])
            counters["n_ok"] += 1
            _save_record(
                saver,
                rs,
                sweep_id,
                matrix.name,
                state["metrics"],
                state["config"],
                None,
            )

    # Single JobWatcher for the whole sweep.  No progress bar — we
    # have our own dashboard.  output_callback replaces the
    # stdout/stderr file routing entirely (see watcher.py: when
    # output_callback is set, lines are dispatched through it
    # instead of being written to the configured streams).
    watcher = JobWatcher(
        handle,
        watch=True,
        progress=False,
        output_callback=on_output,
    )
    for state in state_by_jobid.values():
        watcher.add_jobid(state["jobid"])

    # JobWatcher hooks itself into the reactor via event_watch_async
    # when jobs are added; reactor_run blocks until all watched
    # jobs reach the wait event ("clean" by default — fully exited
    # and KVS cleaned up).
    handle.reactor_run()

    # Finalize: any state that hasn't already signaled completion
    # is a failure — either no ``result`` event was emitted, the
    # child reported ``test.error`` explicitly, or the job's
    # eventlog ended in a non-clean state (jobstatus.status
    # reflects what JobWatcher saw on the eventlog).  Successful
    # runs already fired :meth:`run_completed` live from
    # :func:`_route_line` when the result event arrived.
    for jobid_obj, jobstatus in watcher.progress.jobs.items():
        state = state_by_jobid.get(int(jobid_obj))
        if state is None or state["completed_signaled"]:
            continue
        rs = state["rs"]
        if state["error"]:
            err = state["error"]
        elif jobstatus.status == "failed":
            err = f"job failed (exitcode {jobstatus.exitcode})"
        else:
            err = "child produced no result event"
        emitter.run_failed(rs.run_index, err)
        counters["n_failed"] += 1
        _save_record(saver, rs, sweep_id, matrix.name, None, state["config"], err)

    emitter.sweep_complete()
    return counters["n_ok"], counters["n_failed"]


def _build_sweep_jobspec(argv, num_nodes, name, env_rules=None):
    """Construct a JobspecV1 for one sweep child via the
    :meth:`JobspecV1.from_submit` interface — the same constructor
    used elsewhere in schedbench so all option-handling is
    consistent with the ``flux submit`` CLI.

    Each child runs as a single task on one allocated node with
    exclusive access; the child schedbench process itself spawns
    a subinstance (``flux start``) inside that allocation, so we
    don't need many cores at the outer-job level.  ``cwd`` is
    inherited from the sweep process so the child sees the same
    PATH / PYTHONPATH / FLUX_*_PATH.

    ``env_rules`` is a list of :meth:`from_submit`-style env
    filter rules (``"VAR=VAL"`` / ``"VAR"`` / ``"-PATTERN"`` /
    ``"^/path"``).  ``from_submit`` imports the submitter env by
    default and applies these as additive / subtractive overlays
    — recipe-added variables compose with the existing
    environment instead of replacing it, and ``"VAR=$OTHER..."``
    rules expand against the env built so far.  ``None`` or empty
    means no rules — full submitter env passes through.

    ``unbuffered=True`` is essential: the child emits one JSON
    event per line on stdout, and Python's default block buffering
    on non-TTY stdout would delay events until each buffer fills
    — defeating the point of live JobWatcher output streaming.
    With unbuffered set, each event surfaces in the sweep
    dashboard as soon as the child emits it.

    ``name`` shows up in ``flux jobs`` output so individual sweep
    children are identifiable while the sweep is running (useful
    for debugging hangs and for targeted cancellation).
    """
    from flux.job import JobspecV1

    kwargs = dict(
        ntasks=1,
        nodes=num_nodes,
        cores_per_task=1,
        exclusive=True,
        cwd=os.getcwd(),
        unbuffered=True,
        name=name,
    )
    if env_rules:
        kwargs["env"] = list(env_rules)
    return JobspecV1.from_submit(argv, **kwargs)


def _build_child_argv(rs, flux_schedbench_cmd):
    """Construct argv for one child run.  Forces ``--ui=off`` and
    ``-v`` (verbose) so we get the full event stream including
    progress, and ``--no-save`` so the sweep is the authority on
    what lands in the results file.

    ``flux_schedbench_cmd`` is a list of tokens — typically
    ``["flux", "schedbench"]`` — that prefixes the child invocation.
    """
    argv = list(flux_schedbench_cmd) + rs.argv()
    argv.extend(["--ui=off", "-v", "--no-save"])
    return argv


def _apply_child_event(event, state, run_index, emitter):
    """Apply one parsed JSON event from a child's TestEventEmitter
    to per-run ``state``.  Dispatches ``stage`` / ``progress``
    events to the emitter immediately; ``test.start`` / ``result``
    / ``test.error`` get persisted into the state dict (keys
    ``config`` / ``metrics`` / ``error``).  Used by both the
    serial dispatcher (with a local dict) and the Flux dispatcher
    (with the per-job state dict).
    """
    name = event.get("name", "")
    ctx = event.get("context", {})
    if name in ("stage", "progress"):
        emitter.run_event(run_index, name, ctx)
    elif name == "test.start":
        state["config"] = ctx.get("config")
    elif name == "result":
        state["metrics"] = ctx.get("metrics") or {}
    elif name == "test.error":
        state["error"] = ctx.get("error") or "child reported error"
    # test.complete: nothing to do here; the dispatchers detect
    # completion via either the result event (live) or stream-EOF /
    # eventlog FINISH (in finalization).


def _save_record(saver, rs, sweep_id, sweep_name, metrics, config, error):
    """Build and persist a record for one run.  No-op when
    ``saver`` is ``None`` (e.g. ``--no-save`` and in-memory test
    cases) so callers don't need to repeat the if-saver-then guard
    around every outcome.
    """
    if saver is None:
        return
    saver.add_run(
        _build_record(
            rs,
            sweep_id,
            sweep_name,
            metrics,
            config,
            error,
        )
    )
    saver.save()


def _build_record(rs, sweep_id, sweep_name, metrics, config, error):
    """Assemble the JSON record for one sweep run.

    Mirrors the shape of single-run records produced by
    :func:`flux-schedbench._run_record` so the report tool sees a
    uniform schema, with an additional ``sweep`` sub-object carrying
    the run's matrix position.  Resources / scheduler details come
    from the child's ``test.start`` config (captured during the
    event-stream parse) — no second broker query required.

    Errored runs get the same record shell with ``metrics=None`` and
    an ``error`` field; downstream the report tool can filter them
    out via ``--filter`` or surface them explicitly.
    """
    test_name = rs.test_name()
    cfg = config or {}
    resources = {
        "nodes": cfg.get("nodes", ""),
        "cores_per_node": cfg.get("cores_per_node", ""),
        "gpus_per_node": cfg.get("gpus_per_node", ""),
        "hwloc_xml_path": (
            rs.fixed.get("hwloc_xml_path", "") or rs.axes.get("hwloc_xml_path", "")
        ),
        "amend_r": (rs.fixed.get("amend_r", "") or rs.axes.get("amend_r", "")),
    }
    record = {
        "test_name": test_name,
        "tag": rs.fixed.get("tag", ""),
        "host": socket.gethostname(),
        "user": getpass.getuser(),
        "schedbench_version": "0.1.0",
        "flux_core_version": None,
        "real_exec": cfg.get("real_exec", False),
        "scheduler": {
            "name": cfg.get("scheduler", ""),
            "options": (
                rs.fixed.get("scheduler_options", "")
                or rs.axes.get("scheduler_options", "")
            ),
            "version": None,
        },
        "resources": resources,
        "watcher": cfg.get("watcher", ""),
        "benchmarks": {
            test_name: {
                # The full test.start config dict — duplicates the
                # values used to populate top-level resources /
                # scheduler / etc., plus the per-benchmark fields
                # (njobs, slot_cores, slot_gpus, ...) that aren't
                # represented at the top level.  Persisting it here
                # lets the report tool surface config-only fields on
                # rows whose ``results`` are absent (e.g. an
                # OOM-killed run that never emitted ``result``).
                "config": dict(cfg),
                "results": metrics if metrics is not None else {},
            },
        },
        "sweep": {
            "id": sweep_id,
            "name": sweep_name,
            "run_index": rs.run_index,
            "total": rs.total,
            "axes": rs.axes,
            "fixed": rs.fixed,
        },
    }
    if error is not None:
        record["error"] = error
    return record


# vi: ts=4 sw=4 expandtab
