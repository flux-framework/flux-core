###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import errno

import six

from flux.core.inner import ffi, lib, raw
from flux.wrapper import Wrapper, WrapperPimpl
from flux.util import check_future_error, encode_payload, encode_topic


def encode_rankset(rankset):
    """
    Validate and convert rankset to ascii binary str in proper format
    (e.g., [0,2,3,4,9]).
    Accepts a list of integers or digit strings (i.e., "5")
    Also accepts the shorthands supported by the C API
    (i.e., 'all', 'any', 'upstream')
    """
    if isinstance(rankset, six.text_type):
        rankset = rankset.encode("ascii")
    shorthands = [b"all", b"any", b"upstream"]
    if isinstance(rankset, six.binary_type):
        if rankset not in shorthands:
            errmsg = "Invalid rankset shorthand, must be one of {}"
            raise EnvironmentError(errno.EINVAL, errmsg.format(shorthands))
    else:  # is not shorthand, should be a list of ranks
        if not rankset:
            raise EnvironmentError(errno.EINVAL, "Must supply at least one rank")
        elif not all([isinstance(rank, int) or rank.isdigit() for rank in rankset]):
            raise TypeError("All ranks must be integers")
        else:
            rankcsv = ",".join([str(rank) for rank in rankset])
            rankset = "[{}]".format(rankcsv).encode("ascii")
    return rankset


class MRPC(WrapperPimpl):
    """An MRPC state object"""

    class InnerWrapper(Wrapper):

        # pylint: disable=duplicate-code
        def __init__(self, flux_handle, topic, payload=None, rankset="all", flags=0):
            # hold a reference for destructor ordering
            self._handle = flux_handle
            dest = raw.flux_mrpc_destroy
            super(MRPC.InnerWrapper, self).__init__(
                ffi,
                lib,
                handle=None,
                match=ffi.typeof(lib.flux_mrpc).result,
                prefixes=["flux_mrpc_"],
                destructor=dest,
            )

            # pylint: disable=duplicate-code
            if isinstance(flux_handle, Wrapper):
                flux_handle = flux_handle.handle

            topic = encode_topic(topic)
            payload = encode_payload(payload)
            rankset = encode_rankset(rankset)

            self.handle = raw.flux_mrpc(flux_handle, topic, payload, rankset, flags)

    def __init__(self, flux_handle, topic, payload=None, rankset="all", flags=0):
        super(MRPC, self).__init__()
        self.pimpl = self.InnerWrapper(flux_handle, topic, payload, rankset, flags)
        self.then_args = None
        self.then_cb = None

    def __iter__(self):
        return self

    def next(self):
        return self.__next__()

    # returns a tuple with the nodeid and the response payload
    def __next__(self):
        ret = self.pimpl.next()
        if ret < 0:
            raise StopIteration()
        return (self.get_nodeid(), self.get())

    def get_nodeid(self):
        nodeid = ffi.new("uint32_t [1]")
        self.pimpl.get_nodeid(nodeid)
        return int(nodeid[0])

    @check_future_error
    def get_str(self):
        # pylint: disable=duplicate-code
        resp_str = ffi.new("char *[1]")
        self.pimpl.get(resp_str)
        if resp_str[0] == ffi.NULL:
            return None
        return ffi.string(resp_str[0]).decode("utf-8")

    def get(self):
        # pylint: disable=duplicate-code
        resp_str = self.get_str()
        if resp_str is None:
            return None
        return json.loads(resp_str)

    # not strictly necessary to define, added for better autocompletion
    def check(self):
        return self.pimpl.check()
