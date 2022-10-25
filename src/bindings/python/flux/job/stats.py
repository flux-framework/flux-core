###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from flux.rpc import RPC


class JobStats:
    """Container for job statistics as returned by job-list.job-stats


    Attributes:
        depend: Count of jobs current in DEPEND state
        priority: Count of jobs in PRIORITY state
        sched: Count of jobs in SCHED state
        run: Count of jobs in RUN state
        cleanup: Count of jobs in CLEANUP state
        inactive: Count of INACTIVE jobs
        active: Total number of active jobs (all states but INACTIVE)
        failed: Total number of jobs that did not exit with zero status
        successful: Total number of jobs completed with zero exit code
        canceled: Total number of jobs that were canceled
        timeout: Total number of jobs that timed out
        pending: Sum of "depend", "priority", and "sched"
        running: Sum of "run" and "cleanup"

    """

    def __init__(self, handle):
        """Initialize a JobStats object with Flux handle ``handle``"""
        self.handle = handle
        self.callback = None
        self.cb_kwargs = {}
        for attr in [
            "depend",
            "priority",
            "sched",
            "run",
            "cleanup",
            "inactive",
            "failed",
            "canceled",
            "timeout",
            "pending",
            "running",
            "successful",
            "active",
        ]:
            setattr(self, attr, -1)

    def _update_cb(self, rpc):
        resp = rpc.get()
        for state, count in resp["job_states"].items():
            setattr(self, state, count)
        for state in ["failed", "timeout", "canceled"]:
            setattr(self, state, resp[state])

        #  Compute some stats for convenience:
        #  pylint: disable=attribute-defined-outside-init
        self.pending = self.depend + self.priority + self.sched
        self.running = self.run + self.cleanup
        self.successful = self.inactive - self.failed
        self.active = self.total - self.inactive

        if self.callback:
            self.callback(self, **self.cb_kwargs)

    def _query(self):
        return RPC(self.handle, "job-list.job-stats", {})

    def update(self, callback=None, **kwargs):
        """Asynchronously fetch job statistics and update this object.

        Requires that the reactor for this handle be running in order to
        process the result.

        Args:
            callback: Optional: a callback to call when asynchronous
             update is complete.
            kwargs: Optional: extra keyword arguments to pass to callback()
        """
        self.callback = callback
        self.cb_kwargs = kwargs
        self._query().then(self._update_cb)

    def update_sync(self):
        """Synchronously update job statistics"""
        self._update_cb(self._query())
        return self
