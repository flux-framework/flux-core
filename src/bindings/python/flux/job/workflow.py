###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
from abc import ABC, abstractmethod

import flux.job
from flux.job.Jobspec import _convert_jobspec_arg_to_string
from flux.job import JobID

class JobABC(ABC):
    @abstractmethod
    def submit(self):
        pass

    @abstractmethod
    def wait(self):
        pass

    @abstractmethod
    def cancel(self):
        pass

    @abstractmethod
    def kill(self):
        pass


class Job(JobABC):
    def __init__(self, flux_handle, jobspec):
        self._fh = flux_handle
        self._jobspec_str = _convert_jobspec_arg_to_string(jobspec)
        self._jobspec = None
        self._jobid = None
        self._submitted = False
        self._waitable = False
        self._submit_future = None

    def submit_async(self, **kwargs):
        if self._submitted:
            raise RuntimeError("Cannot submit job twice")
        self._waitable = kwargs.get('waitable', False)
        # TODO: if we supported `and_then` on futures, we could register a
        # callback and grab the jobid after fulfillment while still returning a
        # future to the user and not keeping a reference to the future ourselves
        self._submit_future = flux.job.submit_async(self.handle, self.jobspec_str, **kwargs)
        self._submitted = True
        return self._submit_future

    def submit(self, **kwargs):
        if self._submitted:
            raise RuntimeError("Cannot submit job twice")
        self._waitable = kwargs.get('waitable', False)
        jobid = flux.job.submit(self.handle, self.jobspec_str, **kwargs)
        self._submitted = True
        self._jobid = JobID(jobid)
        return jobid

    @property
    def handle(self):
        return self._fh

    @property
    def waitable(self):
        return self._waitable

    @property
    def jobspec(self):
        if self._jobspec is None:
            self._jobspec = json.loads(self._jobspec_str)
        return self._jobspec

    @property
    def jobspec_str(self):
        return self._jobspec_str

    @property
    def jobid(self):
        # TODO: should getting jobid of unsubmitted job be an exception?
        if self._jobid is None and self._submit_future is not None:
            self._jobid = JobID(self._submit_future.get_id())
            self._submit_future = None
        return self._jobid

    def wait(self):
        if not self._submitted:
            raise RuntimeError("Cannot wait on an unsubmitted job")
        if not self._waitable:
            raise RuntimeError("Cannot wait on job not marked waitable at submit time")
        return flux.job.wait(self.handle, self.jobid)

    def cancel_async(self, reason=None):
        if not self._submitted:
            raise RuntimeError("Cannot cancel an unsubmitted job")
        return flux.job.cancel_async(self.handle, self.jobid, reason=reason)

    def cancel(self, reason=None):
        if not self._submitted:
            raise RuntimeError("Cannot cancel an unsubmitted job")
        return flux.job.cancel(self.handle, self.jobid, reason=reason)

    def kill_async(self, signum=None):
        if not self._submitted:
            raise RuntimeError("Cannot kill an unsubmitted job")
        return flux.job.kill_async(self.handle, self.jobid, signum=signum)

    def kill(self, signum=None):
        if not self._submitted:
            raise RuntimeError("Cannot kill an unsubmitted job")
        return flux.job.kill(self.handle, self.jobid, signum=signum)

    def event_wait(self, name, **kwargs):
        if not self._submitted:
            raise RuntimeError("Cannot wait on events for an unsubmitted job")
        return flux.job.event_wait(self.handle, self.jobid, name, **kwargs)
