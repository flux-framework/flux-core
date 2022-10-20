###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from _flux._security import ffi, lib
from flux.wrapper import FunctionWrapper, Wrapper, WrapperPimpl


class SecurityFunctionWrapper(FunctionWrapper):
    def __call__(self, calling_object, *args_in):
        try:
            return super().__call__(calling_object, *args_in)
        except EnvironmentError:
            errstr = calling_object.last_error()
            errnum = calling_object.last_errnum()
            raise EnvironmentError(errnum, errstr)


class SecurityContext(WrapperPimpl):
    """A Flux Security Context object"""

    class InnerWrapper(Wrapper):
        # pylint: disable=no-value-for-parameter
        def __init__(self, flags=0):
            super().__init__(
                ffi,
                lib,
                match=ffi.typeof("flux_security_t *"),
                prefixes=["flux_security_", "flux_sign_"],
                destructor=lib.flux_security_destroy,
                handle=None,
            )

            self.handle = lib.flux_security_create(flags)

        def check_wrap(self, fun, name):
            fun_type = self.ffi.typeof(fun)
            add_handle = self.check_handle(name, fun_type)
            return SecurityFunctionWrapper(
                fun, name, fun_type, self.ffi, add_handle=add_handle
            )

    def __init__(self, config_pattern=None, flags=0):
        super().__init__()
        self.pimpl = self.InnerWrapper(flags)
        self.pimpl.configure(config_pattern)

    def sign_wrap_as(self, userid, payload, mech_type=ffi.NULL, flags=0):
        if isinstance(payload, str):
            payload = payload.encode("utf-8", errors="surrogateescape")
        elif not isinstance(payload, bytes):
            errstr = "payload must be a text or binary type, not {}"
            raise TypeError(errstr.format(type(payload)))
        return self.pimpl.wrap_as(userid, payload, len(payload), mech_type, flags)

    def sign_wrap(self, payload, mech_type=ffi.NULL, flags=0):
        if isinstance(payload, str):
            payload = payload.encode("utf-8")
        elif not isinstance(payload, bytes):
            errstr = "payload must be a text or binary type, not {}"
            raise TypeError(errstr.format(type(payload)))
        return self.pimpl.wrap(payload, len(payload), mech_type, flags)

    def sign_unwrap(self, signed_payload, flags=0):
        if not isinstance(signed_payload, bytes):
            errstr = "signed_payload must be a binary type, not {}"
            raise TypeError(errstr.format(type(signed_payload)))

        output_payload = ffi.new("void *[1]")  # char**
        output_payload_len = ffi.new("int [1]")  # int*
        output_userid = ffi.new("int64_t [1]")  # int64_t*
        self.pimpl.unwrap(
            signed_payload, output_payload, output_payload_len, output_userid, flags
        )

        # deference int* to int then copy into python int
        output_userid = int(output_userid[0])
        # deference int* to int
        # no need to copy since it doesn't leave this scope
        output_payload_len = output_payload_len[0]
        # deference void** to char* then convert to python binary string
        output_payload = ffi.cast("char *", output_payload[0])
        output_payload = ffi.buffer(output_payload, output_payload_len)

        return (output_payload, output_userid)
