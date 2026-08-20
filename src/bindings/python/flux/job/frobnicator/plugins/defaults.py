##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Apply queue job defaults to incoming jobspec from the job-manager."""

from flux.job.frobnicator import FrobnicatorPlugin
from flux.queue import QueueConf


class DefaultsConfig:
    """Convenience class for handling jobspec defaults configuration"""

    def __init__(self, queue_conf=None):
        # 'queue_conf' is a flux.queue.QueueConf, with each queue's effective
        # defaults (the global defaults overlaid by the queue's own, RFC 33
        # virtual-queue inheritance resolved) already computed.
        self.queue_conf = queue_conf if queue_conf is not None else QueueConf({})
        # The broker will have rejected a config with a default queue that
        # is not also in [queues]. However, protect against it here anyway
        # and raise a sensible error:
        default = self.queue_conf.default_queue
        if default and default not in self.queue_conf:
            raise ValueError(f"default queue '{default}' must be in [queues]")

    def queue_defaults(self, name):
        """Return the effective job defaults for a queue (None = anonymous).

        The job-manager resolves RFC 33 virtual-queue inheritance, so this is
        just the queue's effective ``jobspec.defaults.system``.
        """
        if name is not None and name not in self.queue_conf:
            raise ValueError(f"Invalid queue '{name}' specified")
        return self.queue_conf.defaults(name)

    def setattr_default(self, jobspec, attr, value):
        if attr == "duration" and jobspec.duration == 0:
            jobspec.duration = value
        elif attr not in jobspec.attributes["system"]:
            jobspec.setattr(f"system.{attr}", value)

    def apply_defaults(self, jobspec):
        """Apply general defaults then queue-specific defaults to jobspec"""

        queue = jobspec.queue or self.queue_conf.default_queue or None
        # A falsy QueueConf means no named queues are configured, so the
        # anonymous queue is valid; otherwise a queue is required.
        if queue is None and self.queue_conf:
            raise ValueError("no queue specified")

        for attr, value in self.queue_defaults(queue).items():
            self.setattr_default(jobspec, attr, value)


class Frobnicator(FrobnicatorPlugin):
    def __init__(self, parser):
        self.config = DefaultsConfig()
        super().__init__(parser)

    def configure(self, args, config):
        # queue_conf is injected by the framework as an attribute (see
        # FrobnicatorPlugin) before configure() is called.
        self.config = DefaultsConfig(self.queue_conf)

    def frob(self, jobspec, user, urgency, flags):
        self.config.apply_defaults(jobspec)
