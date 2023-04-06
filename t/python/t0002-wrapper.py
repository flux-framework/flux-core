#!/usr/bin/env python3

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import flux
from flux.core.inner import ffi, raw
import flux.wrapper
from subflux import rerun_under_flux


def __flux_size():
    return 1


class TestWrapper(unittest.TestCase):
    def test_call_non_existent(self):
        f = flux.Flux("loop://")
        with self.assertRaises(flux.wrapper.MissingFunctionError):
            f.non_existent_function_that_should_die("stuff")

    def test_call_insufficient_arguments(self):
        f = flux.Flux("loop://")
        with self.assertRaises(flux.wrapper.WrongNumArguments):
            f.request_encode(self)

    def test_call_invalid_argument_type(self):
        f = flux.Flux("loop://")
        with self.assertRaises(flux.wrapper.InvalidArguments):
            f.request_encode(self, 15)

    def test_null_handle_exception(self):
        f = flux.Flux()
        payload = {"seq": 1, "pad": "stuff"}
        future = f.rpc("broker.ping", payload)
        resp = future.get()
        future.pimpl.handle = None
        with self.assertRaisesRegex(
            ValueError, r"Attempting to call a cached, bound method.*NULL handle"
        ):
            resp = future.get()

    def test_automatic_unwrapping(self):
        flux.core.inner.raw.flux_log(flux.Flux("loop://"), 0, "stuff")

    def test_masked_function(self):
        with self.assertRaisesRegex(AttributeError, r".*masks function.*"):
            flux.Flux("loop://").rpc("topic").pimpl.flux_request_encode("request", 15)

    def test_set_pimpl_handle(self):
        f = flux.Flux("loop://")
        r = f.rpc("topic")
        r.handle = raw.flux_rpc(
            f.handle, "other topic", ffi.NULL, flux.constants.FLUX_NODEID_ANY, 0
        )

    def test_set_pimpl_handle_invalid(self):
        f = flux.Flux("loop://")
        r = f.rpc("topic")
        with self.assertRaisesRegex(TypeError, r".*expected a.*"):
            r.handle = f.rpc("other topic")

    def test_read_basic_value(self):
        self.assertGreater(flux.constants.FLUX_NODEID_ANY, 0)

    def test_invalid_constructor_call(self):
        """Test that a TypeError is raised due to missing arguments rather than a
        infinite recursion as documented in
        https://github.com/flux-framework/flux-core/issues/2485
        """
        with self.assertRaisesRegex(
            TypeError,
            (
                r"(.*takes at least.*arguments.*)"
                r"|(.*missing.*required positional arguments.*)"
            ),
        ):
            flux.wrapper.Wrapper()


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
