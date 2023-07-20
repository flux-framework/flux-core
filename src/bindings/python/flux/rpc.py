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

import flux.constants
from flux.core.inner import ffi, lib, raw
from flux.future import Future
from flux.util import check_future_error, encode_payload, encode_topic, interruptible
from flux.wrapper import Wrapper


class RPC(Future):
    """An RPC state object"""

    class RPCInnerWrapper(Wrapper):
        def __init__(
            self,
            handle=None,
            match=ffi.typeof("flux_future_t *"),
            filter_match=True,
            prefixes=None,
            destructor=raw.flux_future_destroy,
        ):
            # avoid using a static list as a default argument
            # pylint error 'dangerous-default-value'
            if prefixes is None:
                prefixes = ["flux_rpc_", "flux_future_"]

            super().__init__(
                ffi, lib, handle, match, filter_match, prefixes, destructor
            )

        def check_wrap(self, fun, name):
            func = super().check_wrap(fun, name)
            return check_future_error(func)

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
        super(RPC, self).__init__(
            future_handle,
            prefixes=["flux_rpc_", "flux_future_"],
            pimpl_t=self.RPCInnerWrapper,
        )

    @interruptible
    def get_str(self):
        payload_str = ffi.new("char *[1]")
        try:
            self.pimpl.flux_rpc_get(payload_str)
        except OSError:
            self.raise_if_handle_exception()
            raise
        if payload_str[0] == ffi.NULL:
            return None
        return ffi.string(payload_str[0]).decode("utf-8")

    def get(self):
        resp_str = self.get_str()
        if resp_str is None:
            return None
        return json.loads(resp_str)
