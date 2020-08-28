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

from flux.job.info import JobInfo
from flux.rpc import RPC


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
    def get_job(self):
        return self.get()["job"]

    def get_jobinfo(self):
        return JobInfo(self.get_job())


# list-id is not like list or list-inactive, it doesn't return an
# array, so don't use JobListRPC
def job_list_id(flux_handle, jobid, attrs=[]):
    payload = {"id": int(jobid), "attrs": attrs}
    return JobListIdRPC(flux_handle, "job-info.list-id", payload)
