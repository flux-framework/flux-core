###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import os
import pwd

import flux.constants
from flux.job.info import JobInfo
from flux.rpc import RPC


VALID_ATTRS = [
    "userid",
    "priority",
    "t_submit",
    "t_depend",
    "t_sched",
    "t_run",
    "t_cleanup",
    "t_inactive",
    "state",
    "name",
    "ntasks",
    "nnodes",
    "ranks",
    "success",
    "exception_occurred",
    "exception_type",
    "exception_severity",
    "exception_note",
    "result",
    "expiration",
    "annotations",
]


class JobListRPC(RPC):
    def get_jobs(self):
        return self.get()["jobs"]

    def get_jobinfos(self):
        for job in self.get_jobs():
            yield JobInfo(job)


# Due to subtleties in the python bindings and this call, this binding
# is more of a reimplementation of flux_job_list() instead of calling
# the flux_job_list() C function directly.  Some reasons:
#
# - Desire to return a Python RPC class and use its get() method
# - Desired return value is json array, not a single value
#
# pylint: disable=dangerous-default-value
def job_list(
    flux_handle, max_entries=1000, attrs=[], userid=os.getuid(), states=0, results=0
):
    payload = {
        "max_entries": int(max_entries),
        "attrs": attrs,
        "userid": int(userid),
        "states": states,
        "results": results,
    }
    return JobListRPC(flux_handle, "job-info.list", payload)


def job_list_inactive(flux_handle, since=0.0, max_entries=1000, attrs=[], name=None):
    payload = {"since": float(since), "max_entries": int(max_entries), "attrs": attrs}
    if name:
        payload["name"] = name
    return JobListRPC(flux_handle, "job-info.list-inactive", payload)


class JobListIdRPC(RPC):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.jobid = None

    def get_job(self):
        return self.get()["job"]

    def get_jobinfo(self):
        return JobInfo(self.get_job())


# list-id is not like list or list-inactive, it doesn't return an
# array, so don't use JobListRPC
def job_list_id(flux_handle, jobid, attrs=[]):
    payload = {"id": int(jobid), "attrs": attrs}
    rpc = JobListIdRPC(flux_handle, "job-info.list-id", payload)
    #  save original JobId argument for error reporting
    rpc.jobid = jobid
    return rpc



class JobList:
    STATES = {
        "depend": flux.constants.FLUX_JOB_DEPEND,
        "sched": flux.constants.FLUX_JOB_SCHED,
        "run": flux.constants.FLUX_JOB_RUN,
        "cleanup": flux.constants.FLUX_JOB_CLEANUP,
        "inactive": flux.constants.FLUX_JOB_INACTIVE,
        "pending": flux.constants.FLUX_JOB_PENDING,
        "running": flux.constants.FLUX_JOB_RUNNING,
        "active": flux.constants.FLUX_JOB_ACTIVE,
    }
    RESULTS = {
        "completed": flux.constants.FLUX_JOB_RESULT_COMPLETED,
        "failed": flux.constants.FLUX_JOB_RESULT_FAILED,
        "cancelled": flux.constants.FLUX_JOB_RESULT_CANCELLED,
        "timeout": flux.constants.FLUX_JOB_RESULT_TIMEOUT,
    }

    def __init__(
        self,
        flux_handle,
        attrs=VALID_ATTRS,
        filters=[],
        user=None,
        max_entries=1000,
        ids=None,
    ):
        self.handle = flux_handle
        self.attrs = list(attrs)
        self.states = 0
        self.results = 0
        self.max_entries = max_entries
        self.ids = ids
        for fname in filters:
            for name in fname.split(","):
                self.add_filter(name)
        self.set_user(user)

    def set_user(self, user):
        if user is None:
            self.userid = os.getuid()
        elif user == "all":
            self.userid = flux.constants.FLUX_USERID_UNKNOWN
        else:
            try:
                self.userid = pwd.getpwnam(user).pw_uid
            except KeyError:
                self.userid = int(user)
            except ValueError:
                raise ValueError(f"Invalid user {user} specified")

    def add_filter(self, fname):
        fname = fname.lower()
        if fname == "all":
            self.states |= self.STATES["pending"]
            self.states |= self.STATES["running"]
            return

        if fname in self.STATES:
            self.states |= self.STATES[fname]
        elif fname in self.RESULTS:
            # Must specify "inactive" to get results:
            self.states |= self.STATES["inactive"]
            self.results |= self.RESULTS[fname]
        else:
            raise ValueError(f"Invalid filter specified: {fname}")

    def fetch_jobs(self):
        return job_list(
            self.handle,
            max_entries=self.max_entries,
            attrs=self.attrs,
            userid=self.userid,
            states=self.states,
            results=self.results,
        )

    def jobs(self):
        for job in self.fetch_jobs().get_jobs():
            yield JobInfo(job)
