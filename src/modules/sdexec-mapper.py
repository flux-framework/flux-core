#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""sdexec-mapper: resource ID to systemd unit property mapping service.

Provides the ``sdexec-mapper.lookup`` RPC, called by the sdexec module
to translate Flux logical resource IDs (cores, GPUs) to systemd
transient unit properties (AllowedCPUs, AllowedMemoryNodes,
AllowedDevices).

The mapper is initialized lazily on first use by fetching the hwloc
topology XML from ``resource.topo-get``.  Each lookup request supplies
an R object; the local rank's resources are extracted from it and
passed to the mapper.  The default implementation is
:class:`~flux.sdexec.map.HwlocMapper`.  A custom class may be
substituted via broker configuration::

    [sdexec]
    mapper = "mypackage.mymodule.MyMapper"

The custom class must accept the same constructor arguments as
:class:`~flux.sdexec.map.HwlocMapper` (xml).

Load with::

    flux module load sdexec-mapper
"""

import errno
import importlib
import syslog

from flux.brokermod import BrokerModule, request_handler
from flux.sdexec.map import HwlocMapper


def _load_mapper_class(dotted_name):
    """Import and return a mapper class given its fully-qualified name."""
    module_name, _, class_name = dotted_name.rpartition(".")
    if not module_name:
        raise ValueError(
            f"mapper must be a fully-qualified class name: {dotted_name!r}"
        )
    mod = importlib.import_module(module_name)
    return getattr(mod, class_name)


class SdexecMapModule(BrokerModule):
    """Broker module providing resource ID to systemd property mapping."""

    def __init__(self, h, *args):
        super().__init__(h, *args)
        self._xml = None
        self._mapper = None
        self._mapper_class = None
        self._mapper_searchpath = None
        self._requests = 0

    def _init_mapper(self):
        """Initialize the mapper on first use.

        Fetches hwloc topology XML via ``resource.topo-get`` and instantiates
        the configured mapper class with the local broker rank.
        """
        rank = self.handle.get_rank()

        if not self._xml:
            self._xml = self.handle.rpc("resource.topo-get").get_str()
            if self._xml is None:
                raise OSError(errno.ENODATA, "topo-get returned no payload")

        # Need to ensure we have an updated config on first conf_get():
        name = self.handle.conf_get("sdexec.mapper", default=None, update=True)
        searchpath = self.handle.conf_get("sdexec.mapper-searchpath", default="")

        # Update sys.path if searchpath configured
        if searchpath:
            import sys

            for path in searchpath.split(":"):
                if path and path not in sys.path:
                    sys.path.insert(0, path)

        if name:
            cls = _load_mapper_class(name)
            self._mapper_class = name
        else:
            cls = HwlocMapper
            self._mapper_class = "flux.sdexec.map.HwlocMapper"

        self._mapper_searchpath = searchpath
        self._mapper = cls(self._xml, rank)

    @request_handler("lookup")
    def lookup(self, msg):
        """Handle sdexec-mapper.lookup requests.

        Expected payload: ``{"R": <R JSON object>}`` where R describes the
        resources allocated to the job.  The local broker rank's resources are
        extracted from R and passed to the mapper.

        Response payload: dict of systemd unit property names to values,
        e.g. ``{"AllowedCPUs": "0-3", "AllowedMemoryNodes": "0",
        "AllowedDevices": ["char /dev/dri/renderD128 rw"]}``.
        """
        try:
            if self._mapper is None:
                self._init_mapper()
            result = self._mapper.map(msg.payload["R"])
            self._requests += 1
            self.handle.respond(msg, result)
        except OSError as exc:
            self.log(syslog.LOG_ERR, f"lookup failed: {exc}")
            self.handle.respond_error(msg, exc.errno or errno.EINVAL, str(exc))
        except Exception as exc:
            self.log(syslog.LOG_ERR, f"lookup failed: {exc}")
            self.handle.respond_error(msg, errno.EINVAL, str(exc))

    @request_handler("config-reload")
    def config_reload(self, msg):
        """Handle sdexec-mapper.config-reload requests.

        Resets the mapper so it will be reinitialized on next lookup
        with the new configuration.
        """
        try:
            self.log(
                syslog.LOG_DEBUG, "resetting mapper state due to config-reload request"
            )
            self._mapper = None
            self._mapper_class = None
            self._mapper_searchpath = None
            self.handle.respond(msg)
        except Exception as exc:
            self.log(syslog.LOG_ERR, f"config-reload failed: {exc}")
            self.handle.respond_error(msg, errno.EINVAL, str(exc))

    @request_handler("stats-get")
    def stats_get(self, msg):
        """Handle sdexec-mapper.stats-get requests.

        Returns module statistics including mapper configuration and
        request count.
        """
        try:
            if not self._mapper:
                self._init_mapper()
            stats = {
                "config": {
                    "mapper_class": self._mapper_class,
                    "mapper_searchpath": self._mapper_searchpath or "",
                },
                "requests": self._requests,
            }
            self.handle.respond(msg, stats)
        except Exception as exc:
            self.log(syslog.LOG_ERR, f"stats-get failed: {exc}")
            self.handle.respond_error(msg, errno.EINVAL, str(exc))

    def log(self, level, msg):
        self.handle.log(level, msg)
