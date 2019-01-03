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

from flux.wrapper import Wrapper, WrapperPimpl
from flux.core.inner import ffi, lib, raw
import flux.constants
from flux.util import check_future_error, encode_payload, encode_topic


class RPC(WrapperPimpl):
    """An RPC state object"""

    class InnerWrapper(Wrapper):
        def __init__(
            self,
            flux_handle,
            topic,
            payload=None,
            nodeid=flux.constants.FLUX_NODEID_ANY,
            flags=0,
        ):
            # hold a reference for destructor ordering
            self._handle = flux_handle
            dest = raw.flux_future_destroy
            super(RPC.InnerWrapper, self).__init__(
                ffi,
                lib,
                handle=None,
                match=ffi.typeof(lib.flux_rpc).result,
                prefixes=["flux_rpc_"],
                destructor=dest,
            )
            if isinstance(flux_handle, Wrapper):
                flux_handle = flux_handle.handle

            topic = encode_topic(topic)
            payload = encode_payload(payload)

            self.handle = raw.flux_rpc(flux_handle, topic, payload, nodeid, flags)

    def __init__(
        self,
        flux_handle,
        topic,
        payload=None,
        nodeid=flux.constants.FLUX_NODEID_ANY,
        flags=0,
    ):
        super(RPC, self).__init__()
        self.pimpl = self.InnerWrapper(flux_handle, topic, payload, nodeid, flags)
        self.then_args = None
        self.then_cb = None

    # TODO: replace with first-class future support
    @check_future_error
    def get_str(self):
        j_str = ffi.new("char *[1]")
        self.pimpl.get(j_str)
        if j_str[0] == ffi.NULL:
            return None
        return ffi.string(j_str[0]).decode("utf-8")

    def get(self):
        resp_str = self.get_str()
        if resp_str is None:
            return None
        return json.loads(resp_str)
