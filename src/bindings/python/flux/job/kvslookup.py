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

import flux.constants
from _flux._core import ffi, lib
from flux.future import WaitAllFuture
from flux.job import JobID
from flux.rpc import RPC


def _decode_field(data, key):
    try:
        tmp = json.loads(data[key])
        data[key] = tmp
    except json.decoder.JSONDecodeError:
        # Ignore if can't be decoded
        pass


# a few keys are special, decode them into dicts if you can
def decode_special_data(data):
    for key in ("jobspec", "R"):
        if key in data:
            _decode_field(data, key)


class JobInfoLookupRPC(RPC):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.jobid = None

    def get(self):
        return super().get()

    def get_decode(self):
        data = super().get()
        decode_special_data(data)
        return data


def job_info_lookup(flux_handle, jobid, keys=["jobspec"], flags=0):
    payload = {"id": int(jobid), "keys": keys, "flags": flags}
    rpc = JobInfoLookupRPC(flux_handle, "job-info.lookup", payload)
    rpc.jobid = jobid
    return rpc


def _setup_lookup_keys(keys, original):
    if "jobspec" in keys:
        if original:
            keys.remove("jobspec")
            if "J" not in keys:
                keys.append("J")


def _get_original_jobspec(job_data):
    J = bytes(job_data["J"], encoding="utf8")
    val = lib.flux_unwrap_string(J, False, ffi.NULL, ffi.NULL)
    result = ffi.string(val)
    lib.free(val)
    return result.decode("utf-8")


def _update_keys(job_data, decode, keys, original):
    if "jobspec" in keys:
        if original:
            job_data["jobspec"] = _get_original_jobspec(job_data)
            if decode:
                _decode_field(job_data, "jobspec")
            if "J" not in keys:
                job_data.pop("J")


# jobs_kvs_lookup simple variant for one jobid
def job_kvs_lookup(
    flux_handle, jobid, keys=["jobspec"], decode=True, original=False, base=False
):
    """
    Lookup job kvs data based on a jobid

    Some keys such as "jobspec" or "R" may be altered based on update events
    in the eventlog.  Set 'base' to True to skip these updates and
    read exactly what is in the KVS.

    :flux_handle: A Flux handle obtained from flux.Flux()
    :jobid: jobid to lookup info for
    :keys: Optional list of keys to fetch. (default is "jobspec")
    :decode: Optional flag to decode special data into Python data structures
             currently decodes "jobspec" and "R" into dicts
             (default True)
    :original: For 'jobspec', return the original submitted jobspec
    :base: For 'jobspec' or 'R', get base value, do not apply updates from eventlog
    """
    keyslookup = list(keys)
    _setup_lookup_keys(keyslookup, original)
    # N.B. JobInfoLookupRPC() has a "get_decode()" member
    # function, so we will always get the non-decoded result from
    # job-info.
    flags = 0
    if not base:
        flags |= flux.constants.FLUX_JOB_LOOKUP_CURRENT
    payload = {"id": int(jobid), "keys": keyslookup, "flags": flags}
    rpc = JobInfoLookupRPC(flux_handle, "job-info.lookup", payload)
    try:
        if decode:
            rsp = rpc.get_decode()
        else:
            rsp = rpc.get()
        _update_keys(rsp, decode, keys, original)
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

    Some keys such as "jobspec" or "R" may be altered based on update events
    in the eventlog.  Set 'base' to True to skip these updates and
    read exactly what is in the KVS.

    :flux_handle: A Flux handle obtained from flux.Flux()
    :ids: List of jobids to get data for
    :keys: Optional list of keys to fetch. (default is "jobspec")
    :decode: Optional flag to decode special data into Python data structures
             currently decodes "jobspec" and "R" into dicts
             (default True)
    :original: For 'jobspec', return the original submitted jobspec
    :base: For 'jobspec' or 'R', get base value, do not apply updates from eventlog
    """

    def __init__(
        self,
        flux_handle,
        ids=[],
        keys=["jobspec"],
        decode=True,
        original=False,
        base=False,
    ):
        self.handle = flux_handle
        self.keys = list(keys)
        self.keyslookup = list(keys)
        self.ids = list(map(JobID, ids)) if ids else []
        self.decode = decode
        self.original = original
        self.base = base
        self.errors = []
        _setup_lookup_keys(self.keyslookup, self.original)

    def fetch_data(self):
        """Initiate the job info lookup to the Flux job-info module

        JobKVSLookup.fetch_data() returns a JobKVSLookupFuture,
        which will be fulfilled when the job data is available.

        Once the Future has been fulfilled, a list of objects
        can be obtained via JobKVSLookup.data(). If
        JobKVSLookupFuture.errors is non-empty, then it will contain a
        list of errors returned via the query.
        """
        flags = 0
        # N.B. JobInfoLookupRPC() has a "get_decode()" member
        # function, so we will always get the non-decoded result from
        # job-info.
        if not self.base:
            flags |= flux.constants.FLUX_JOB_LOOKUP_CURRENT
        listids = JobKVSLookupFuture()
        for jobid in self.ids:
            listids.push(job_info_lookup(self.handle, jobid, self.keyslookup, flags))
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
        for job_data in data:
            _update_keys(job_data, self.decode, self.keys, self.original)
        return data
