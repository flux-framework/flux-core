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


# pylint: disable=too-many-instance-attributes
class JobStats:
    """Container for job statistics as returned by job-list.job-stats


    Attributes:
        depend: Count of jobs current in DEPEND state
        priority: Count of jobs in PRIORITY state
        sched: Count of jobs in SCHED state
        run: Count of jobs in RUN state
        cleanup: Count of jobs in CLEANUP state
        inactive: Count of INACTIVE jobs
        successful: Total number of jobs completed with zero exit code
        failed: Total number of jobs that did not exit with zero status
        timeout: Total number of jobs that timed out
        canceled: Total number of jobs that were canceled
        pending: Sum of "depend", "priority", and "sched"
        running: Sum of "run" and "cleanup"
        active: Total number of active jobs (all states but INACTIVE)

    """

    def __init__(self, handle, queue=None):
        """Initialize a JobStats object with Flux handle ``handle``"""
        self.handle = handle
        self.queue = queue
        self.callback = None
        self.cb_kwargs = {}
        for attr in [
            "depend",
            "priority",
            "sched",
            "run",
            "cleanup",
            "inactive",
            "successful",
            "failed",
            "timeout",
            "canceled",
            "inactive_purged",
            "pending",
            "running",
            "active",
        ]:
            setattr(self, attr, -1)

    def _update_cb(self, rpc):
        resp = rpc.get()
        if self.queue:
            tmpstat = None
            if resp["queues"]:
                tmpstat = [x for x in resp["queues"] if x["name"] == self.queue]
            if not tmpstat:
                raise ValueError(f"no stats available for queue {self.queue}")
            resp = tmpstat[0]

        for state, count in resp["job_states"].items():
            setattr(self, state, count)
        for state in ["successful", "failed", "timeout", "canceled", "inactive_purged"]:
            setattr(self, state, resp[state])

        #  Compute some stats for convenience:
        #  pylint: disable=attribute-defined-outside-init
        self.pending = self.depend + self.priority + self.sched
        self.running = self.run + self.cleanup
        self.active = self.total - self.inactive

        # This class reports the total number of unsuccessful jobs in
        # the 'failed' attribute, not just the count of jobs that ran
        # to completion with nonzero exit code
        self.failed += self.timeout
        self.failed += self.canceled

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
