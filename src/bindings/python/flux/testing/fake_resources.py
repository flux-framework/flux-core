###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Fake resource generation and installation for testing.

Provides a :class:`FakeResources` ABC describing a synthetic resource set with
a flat shape (nodes × cores × gpus), plus an :class:`InjectFakeResources`
implementation that encodes R via ``flux R encode`` and writes it to the
``resource.R`` KVS key. Intended for use by the ``fake-resources`` modprobe
rc1 task (see flux-config-fake-resources(5)), which runs this code before the
resource module loads so the synthetic R is in place at broker startup.
"""

import abc
import importlib
import json
import os
import subprocess
import sys

import flux
import flux.kvs
from flux.idset import IDset
from flux.importer import import_path


def _stderr_log(msg):
    """Default log function: print ``msg`` to stderr.

    The CLI overrides this with ``LOGGER.info`` (or similar) for prefixed
    output; tests pass a no-op to suppress noise.
    """
    print(msg, file=sys.stderr)


def load_amender(spec):
    """Resolve a TOML ``amend-r`` spec to a callable.

    Two forms are supported:

    * ``module.path:function_name`` — imports ``module.path`` and returns
      its ``function_name`` attribute. Useful when the amender ships with
      a Python package on PYTHONPATH (e.g. a Fluxion JGF helper).

    * Anything without a ``:`` is treated as a filesystem path; the file
      is loaded as a Python module and its ``amend`` callable is returned.
      Useful for ad-hoc test amenders that don't warrant a package.

    The returned callable should have signature ``amend(R, hwloc_xml=None) ->
    R``. It is invoked from :meth:`FakeResources.amend_R` when set via the
    ``amender`` constructor argument.

    Raises :class:`RuntimeError` with a descriptive message on any failure
    (file not found, import error, missing attribute), so configuration
    mistakes surface clearly from the modprobe rc1 task rather than as a
    generic ImportError.
    """
    if ":" in spec:
        mod_name, fn_name = spec.split(":", 1)
        try:
            module = importlib.import_module(mod_name)
        except ImportError as exc:
            raise RuntimeError(
                "[fake-resources] amend-r: failed to import "
                f"module {mod_name!r}: {exc}"
            )
        if not hasattr(module, fn_name):
            raise RuntimeError(
                f"[fake-resources] amend-r: module {mod_name!r} "
                f"has no attribute {fn_name!r}"
            )
        return getattr(module, fn_name)

    # File-path form
    path = os.path.expanduser(spec)
    if not os.path.isfile(path):
        raise RuntimeError(f"[fake-resources] amend-r: file not found: {path!r}")
    try:
        module = import_path(path)
    except Exception as exc:
        raise RuntimeError(f"[fake-resources] amend-r: failed to load {path!r}: {exc}")
    if not hasattr(module, "amend"):
        raise RuntimeError(
            f"[fake-resources] amend-r: file {path!r} has no "
            "'amend' callable at module scope"
        )
    return module.amend


class FakeResources(abc.ABC):
    """Abstract base for synthetic resource sets with a flat shape.

    A resource set described as a count of nodes, cores per node, and
    (optionally) GPUs per node. On-node topology (sockets, NUMA domains) is
    not modeled here; subclasses or callers needing topology should pass real
    hwloc XML, which ``flux R encode --local`` will consume, or override
    :meth:`amend_R` to inject scheduler-specific metadata.

    Subclasses must implement :meth:`install`, which makes the resource set
    visible to the broker. Subclasses may also override :meth:`amend_R` to
    mutate the encoded R before it is installed.
    """

    def __init__(
        self, nodes, cores_per_node=1, gpus_per_node=0, host_prefix="fake", amender=None
    ):
        self.nodes = nodes
        self.cores_per_node = cores_per_node
        self.gpus_per_node = gpus_per_node
        self.host_prefix = host_prefix
        # amender may be a callable (caller has already resolved it,
        # or built it inline) or a string in the load_amender(spec)
        # forms (``module:function`` or a path). String form is
        # resolved here so the rc1 task and other callers can just
        # forward the TOML value without thinking about resolution.
        if isinstance(amender, str):
            amender = load_amender(amender)
        self.amender = amender

    @property
    def total_cores(self):
        """Total core count across all nodes."""
        return self.nodes * self.cores_per_node

    @property
    def total_gpus(self):
        """Total GPU count across all nodes."""
        return self.nodes * self.gpus_per_node

    @abc.abstractmethod
    def install(self):
        """Make these resources visible to the broker.

        Concrete subclasses may extend this signature with additional
        arguments.
        """

    def saturation_count(self, slot_cores=1, slot_gpus=0):
        """Number of slot-shape jobs needed to saturate this resource set.

        See :func:`saturation_count` for the algorithm. This method is a thin
        wrapper that pulls ``nodes``, ``cores_per_node``, and
        ``gpus_per_node`` from ``self``; it exists so callers holding a
        FakeResources object don't have to unpack its attributes. The function
        form is what ``flux schedbench`` calls for the real-resource path,
        where there's no FakeResources object — the shape comes from a
        ``flux.resource.resource_list`` query.
        """
        return saturation_count(
            self.nodes,
            self.cores_per_node,
            self.gpus_per_node,
            slot_cores=slot_cores,
            slot_gpus=slot_gpus,
        )

    def amend_R(self, R, hwloc_xml=None):
        """Hook to mutate R after generation, before KVS install.

        Subclasses can override directly for complex amendment logic. Callers
        (typically the fake-resources modprobe rc1 task) can also pass
        ``amender=`` to the constructor to inject a callable taking ``(R,
        hwloc_xml=...)`` and returning amended R; the default :meth:`amend_R`
        defers to it. Subclass overrides take precedence; a subclass that
        wants to incorporate ``self.amender`` should call ``super().amend_R(R,
        hwloc_xml=...)``.

        Used to inject scheduler-specific metadata into R — for example,
        Fluxion JGF keys or property tags. When called against an XML-derived
        R, ``hwloc_xml`` is the loaded XML string; otherwise it is None.
        """
        if self.amender is not None:
            return self.amender(R, hwloc_xml=hwloc_xml)
        return R


def saturation_count(nodes, cores_per_node, gpus_per_node, slot_cores=1, slot_gpus=0):
    """Return the number of slot-shape jobs that saturate a flat
    resource set.

    A ``slot`` is described by its core and GPU counts; this returns the
    number of such slots that fit across ``nodes`` machines of
    ``cores_per_node`` cores and ``gpus_per_node`` GPUs each, taking the
    more-constraining of cores and GPUs into account. Pure arithmetic — no
    broker access, no FakeResources object required — so it's reusable from
    code paths that gather the shape from :func:`flux.resource.resource_list`
    instead.

    Either ``slot_cores`` or ``slot_gpus`` must be positive.

    Non-uniformity note: this assumes uniform cores/GPUs per node. Real broker
    resources may not be uniform; callers that derive the shape from a broker
    query typically average across nodes, which can over- or under-estimate
    true capacity by one slot per heterogeneous node. The error is bounded and
    not a correctness issue for benchmarks that measure aggregate rates.
    """
    if slot_cores < 1 and slot_gpus < 1:
        raise ValueError("slot_cores or slot_gpus must be positive")
    per_node = cores_per_node // max(slot_cores, 1)
    if slot_gpus:
        per_node = min(per_node, gpus_per_node // slot_gpus)
    return nodes * per_node


def build_R_encode_args(fr):
    """Build argv for ``flux R encode`` from a :class:`FakeResources`.

    Exposed separately from :class:`InjectFakeResources` so unit tests can
    verify command construction without requiring a broker.

    Idset-shaped arguments (``-r``, ``-c``, ``-g``) are formatted via
    :class:`flux.idset.IDset` rather than ``f"0-{n-1}"``. A degenerate
    single-element range like ``"0-0"`` is rejected when ``flux R encode``
    parses it as an idset, which silently produced R without the affected
    resource type (the ``gpus_per_node=1`` case originally surfaced this).
    :class:`IDset` emits ``"0"`` for a one-element set, which parses cleanly.
    """
    n = fr.nodes
    ranks = IDset()
    ranks.set(0, n - 1)
    cores = IDset()
    cores.set(0, fr.cores_per_node - 1)
    cmd = [
        "flux",
        "R",
        "encode",
        f"-r{ranks}",
        f"-H{fr.host_prefix}[0-{n - 1}]",
        "-c",
        str(cores),
    ]
    if fr.gpus_per_node > 0:
        gpus = IDset()
        gpus.set(0, fr.gpus_per_node - 1)
        cmd += ["-g", str(gpus)]
    return cmd


class InjectFakeResources(FakeResources):
    """:class:`FakeResources` that writes synthetic R to the KVS.

    :meth:`install` encodes R from the configured shape (or from an hwloc XML
    file, if ``hwloc_xml_path`` is set) and writes it to ``resource.R``. The
    caller is responsible for arranging the write to happen before the
    resource module reads its initial R, and for setting any resource-module
    options needed to accept synthetic R (``noverify``, ``monitor-force-up``).
    The ``fake-resources`` modprobe rc1 task handles both.

    If ``hwloc_xml_path`` is set, R is encoded by passing the XML file to
    ``flux R encode --xml=FILE`` rather than from the numeric
    ``cores_per_node`` / ``gpus_per_node`` fields. The XML's loaded contents
    are passed to :meth:`amend_R` so subclasses can use them.

    The ``log`` argument is a callable taking a single string (default
    :func:`_stderr_log`). Pass ``LOGGER.info`` to integrate with a CLI's
    logging configuration, or ``lambda _: None`` to suppress output entirely.

    **Writing an amender**

    An *amender* is a Python callable that mutates the encoded R before it is
    written to the KVS. Use it to inject scheduler-specific metadata that the
    bare ``flux R encode`` output doesn't carry, most notably the contents of
    the scheduling key, but may also include things like node properties or
    custom attributes for property-based schedulers.

    An amender has the signature::

    def amend(R, hwloc_xml=None): # mutate R in place, or build a new dict
        R["scheduling"] = {...}
        return R

    Where:

    * ``R`` is the parsed R dict (RFC 20) produced by ``flux R encode``,
      ready to mutate. The amender owns it for the duration of the call;
      either mutate in place and return the same dict, or build a new one
      and return that.
    * ``hwloc_xml`` is the contents of the hwloc XML file when
      ``hwloc_xml_path`` was set on the constructor, or None otherwise.
      Useful for amenders that derive their additions from the topology
      (Fluxion's JGF, for example, walks the XML to build a graph).
    * The return value is what gets written to ``resource.R`` in KVS.

    The amender is passed to the constructor as the ``amender`` argument and
    may be either:

    * A callable, ready to invoke. Use this when the amender is defined in
      the same source file or pulled from a Python package the caller
      already imported.
    * A string in one of two forms, resolved at construction time via
      :func:`load_amender`:

      - ``"module.path:function_name"`` — importlib loads ``module.path``
        and looks up ``function_name``. Use this when the amender ships in
        a package on PYTHONPATH.
      - Anything without a colon is treated as a filesystem path; the file
        is loaded as a Python module and its ``amend`` (literally that
        name) attribute is used. Use this for ad-hoc test amenders or for
        passing an amender from a modprobe config without packaging it.

    The string forms are what the ``[fake-resources]`` ``amend-r`` TOML key in
    flux-config-fake-resources(5) accepts; the rc1 task forwards the TOML
    value straight to this constructor.

    Subclassing :class:`FakeResources` and overriding :meth:`amend_R` is an
    alternative to passing an amender — choose subclassing when the amendment
    logic is non-trivial (multiple methods, internal state) and the callable
    form when the logic fits in one function. Subclass overrides take
    precedence over the constructor ``amender``; a subclass that wants to
    incorporate the constructor amender should call ``super().amend_R(R,
    hwloc_xml=...)``.
    """

    def __init__(
        self,
        nodes,
        cores_per_node=1,
        gpus_per_node=0,
        host_prefix="fake",
        hwloc_xml_path=None,
        verbose=False,
        log=None,
        amender=None,
    ):
        super().__init__(
            nodes=nodes,
            cores_per_node=cores_per_node,
            gpus_per_node=gpus_per_node,
            host_prefix=host_prefix,
            amender=amender,
        )
        self.hwloc_xml_path = hwloc_xml_path
        self.verbose = verbose
        self.log = log if log is not None else _stderr_log

    def install(self, handle=None):
        """Encode R from the configured shape and install it into the
        ``resource.R`` KVS key.

        Args:
            handle: an open :class:`flux.Flux` handle used for the KVS
            put + commit. If ``None``, a fresh handle is opened via
            :class:`flux.Flux()`.
        """
        r_json, hwloc_xml = self._encode_R()
        r_json = self._apply_amend_R(r_json, hwloc_xml)
        self._install_R(r_json, handle=handle)
        self.log("Fake resources injected.")

    def _encode_R(self):
        """
        Run ``flux R encode`` and return ``(r_json, hwloc_xml_or_None)``.
        """
        if self.hwloc_xml_path:
            # The XML describes a single machine's topology. We pass
            # ``-r`` and ``-H`` alongside ``--xml=FILE`` so flux R encode
            # replicates that per-node shape across the configured rank
            # range, yielding a ``self.nodes``-rank R with hostnames
            # under the configured ``host_prefix`` instead of the
            # Machine HostName carried in the XML.
            cmd = ["flux", "R", "encode", f"--xml={self.hwloc_xml_path}"]
            if self.nodes == 1:
                cmd += ["-r", "0", "-H", f"{self.host_prefix}0"]
            else:
                cmd += [
                    "-r",
                    f"0-{self.nodes - 1}",
                    "-H",
                    f"{self.host_prefix}[0-{self.nodes - 1}]",
                ]
            if self.verbose:
                self.log("+ " + " ".join(cmd))
            result = subprocess.run(cmd, stdout=subprocess.PIPE, check=True)
            with open(self.hwloc_xml_path) as f:
                hwloc_xml = f.read()
            return result.stdout, hwloc_xml

        cmd = build_R_encode_args(self)
        self.log(
            f"Encoding fake R: {self.nodes} nodes, "
            f"{self.cores_per_node} cores/node, "
            f"{self.gpus_per_node} GPUs/node"
        )
        if self.verbose:
            self.log("+ " + " ".join(cmd))
        result = subprocess.run(cmd, stdout=subprocess.PIPE, check=True)
        return result.stdout, None

    def _apply_amend_R(self, r_json, hwloc_xml):
        """Round-trip R through the amend_R hook."""
        R = json.loads(r_json)
        R = self.amend_R(R, hwloc_xml=hwloc_xml)
        return json.dumps(R).encode()

    def _install_R(self, r_json, handle=None):
        """Write R into the ``resource.R`` KVS key."""
        if handle is None:
            handle = flux.Flux()
        if self.verbose:
            self.log("+ kvs put resource.R")
        flux.kvs.put(handle, "resource.R", r_json, raw=True)
        flux.kvs.commit(handle)


# vi: ts=4 sw=4 expandtab
