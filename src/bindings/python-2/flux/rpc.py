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

from flux.wrapper import Wrapper
from flux.future import Future
from flux.core.inner import ffi, raw
import flux.constants
from flux.util import encode_payload, encode_topic


class RPC(Future):
    """An RPC state object"""

    def __init__(
        self,
        flux_handle,
        topic,
        payload=None,
        nodeid=flux.constants.FLUX_NODEID_ANY,
        flags=0,
    ):
        if isinstance(flux_handle, Wrapper):
            # keep the flux_handle alive for the lifetime of the RPC
            self.flux_handle = flux_handle
            flux_handle = flux_handle.handle

        topic = encode_topic(topic)
        payload = encode_payload(payload)

        future_handle = raw.flux_rpc(flux_handle, topic, payload, nodeid, flags)
        super(RPC, self).__init__(future_handle, prefixes=["flux_rpc_", "flux_future_"])

    def get_str(self):
        payload_str = ffi.new("char *[1]")
        self.pimpl.flux_rpc_get(payload_str)
        if payload_str[0] == ffi.NULL:
            return None
        return ffi.string(payload_str[0]).decode("utf-8")

    def get(self):
        resp_str = self.get_str()
        if resp_str is None:
            return None
        return json.loads(resp_str)
