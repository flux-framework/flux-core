###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import errno
import json

from flux.future import WaitAllFuture
from flux.job import JobID
from flux.rpc import RPC


# a few keys are special, decode them into dicts if you can
def decode_special_metadata(metadata):
    for key in ("jobspec", "R"):
        if key in metadata:
            try:
                tmp = json.loads(metadata[key])
                metadata[key] = tmp
            except json.decoder.JSONDecodeError:
                # Ignore if can't be decoded
                pass


class JobInfoLookupRPC(RPC):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.jobid = None

    def get(self):
        return super().get()

    def get_decode(self):
        metadata = super().get()
        decode_special_metadata(metadata)
        return metadata


def job_info_lookup(flux_handle, jobid, keys=["jobspec"]):
    payload = {"id": int(jobid), "keys": keys, "flags": 0}
    rpc = JobInfoLookupRPC(flux_handle, "job-info.lookup", payload)
    rpc.jobid = jobid
    return rpc


# jobs_kvs_lookup simple variant for one jobid
def job_kvs_lookup(flux_handle, jobid, keys=["jobspec"], decode=True):
    """
    Lookup job kvs data based on a jobid

    :flux_handle: A Flux handle obtained from flux.Flux()
    :jobid: jobid to lookup info for
    :keys: Optional list of keys to fetch. (default is "jobspec")
    :decode: Optional flag to decode special data into Python data structures
             currently decodes "jobspec" and "R" into dicts
             (default True)
    """
    payload = {"id": int(jobid), "keys": keys, "flags": 0}
    rpc = JobInfoLookupRPC(flux_handle, "job-info.lookup", payload)
    try:
        if decode:
            rsp = rpc.get_decode()
        else:
            rsp = rpc.get()
    # The job does not exist!
    except FileNotFoundError:
        return None
    return rsp


class JobKVSLookupFuture(WaitAllFuture):
    """Wrapper Future for multiple jobids"""

    def __init__(self):
        super(JobKVSLookupFuture, self).__init__()
        self.errors = []

    def _get(self, decode=True):
        jobs = []
        #  Wait for all RPCs to complete
        self.wait_for()

        #  Get all successful jobs, accumulate errors in self.errors
        for child in self.children:
            try:
                if decode:
                    rsp = child.get_decode()
                else:
                    rsp = child.get()
                jobs.append(rsp)
            except EnvironmentError as err:
                if err.errno == errno.ENOENT:
                    msg = f"JobID {child.jobid.orig} unknown"
                else:
                    msg = f"rpc: {err.strerror}"
                self.errors.append(msg)
        return jobs

    def get(self):
        """get all successful results, appending errors into self.errors"""
        return self._get(False)

    def get_decode(self):
        """
        get all successful results, appending errors into self.errors.  Decode
        special data into Python data structures
        """
        return self._get(True)


class JobKVSLookup:
    """User friendly class to lookup job KVS data

    :flux_handle: A Flux handle obtained from flux.Flux()
    :ids: List of jobids to get data for
    :keys: Optional list of keys to fetch. (default is "jobspec")
    :decode: Optional flag to decode special data into Python data structures
             currently decodes "jobspec" and "R" into dicts
             (default True)
    """

    def __init__(
        self,
        flux_handle,
        ids=[],
        keys=["jobspec"],
        decode=True,
    ):
        self.handle = flux_handle
        self.keys = list(keys)
        self.ids = list(map(JobID, ids)) if ids else []
        self.decode = decode
        self.errors = []

    def fetch_data(self):
        """Initiate the job info lookup to the Flux job-info module

        JobKVSLookup.fetch_data() returns a JobKVSLookupFuture,
        which will be fulfilled when the job data is available.

        Once the Future has been fulfilled, a list of objects
        can be obtained via JobKVSLookup.data(). If
        JobKVSLookupFuture.errors is non-empty, then it will contain a
        list of errors returned via the query.
        """
        listids = JobKVSLookupFuture()
        for jobid in self.ids:
            listids.push(job_info_lookup(self.handle, jobid, self.keys))
        return listids

    def data(self):
        """Synchronously fetch a list of data responses

        If the Future object returned by JobKVSLookup.fetch_data has
        not yet been fulfilled (e.g. is_ready() returns False), then this call
        may block.  Otherwise, returns a list of responses for all job ids
        returned.
        """
        rpc = self.fetch_data()
        if self.decode:
            data = rpc.get_decode()
        else:
            data = rpc.get()
        if hasattr(rpc, "errors"):
            self.errors = rpc.errors
        return data
