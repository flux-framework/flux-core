###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import flux.kvs
from flux.job._wrapper import _RAW as RAW
from _flux._core import ffi


def job_kvs(flux_handle, jobid):
    """
    :returns: The KVS directory of the given job
    :rtype: KVSDir
    """

    path_len = 1024
    buf = ffi.new("char[]", path_len)
    RAW.kvs_key(buf, path_len, jobid, "")
    kvs_key = ffi.string(buf, path_len).decode("utf-8")
    return flux.kvs.get_dir(flux_handle, kvs_key)


def job_kvs_guest(flux_handle, jobid):
    """
    :returns: The KVS guest directory of the given job
    :rtype: KVSDir
    """

    path_len = 1024
    buf = ffi.new("char[]", path_len)
    RAW.kvs_guest_key(buf, path_len, jobid, "")
    kvs_key = ffi.string(buf, path_len).decode("utf-8")
    return flux.kvs.get_dir(flux_handle, kvs_key)
