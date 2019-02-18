#!/usr/bin/env python

###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from __future__ import print_function

import gc
import errno
import unittest

import flux
import flux.constants
from flux.core.inner import ffi
from flux.future import Future
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestHandle(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        """Create a handle, connect to flux"""
        self.f = flux.Flux()

    @classmethod
    def tearDownClass(self):
        self.f.close()

    def test_01_rpc_get(self):
        payload = {"seq": 1, "pad": "stuff"}
        future = self.f.rpc("cmb.ping", payload)
        resp_payload = future.get()
        self.assertDictContainsSubset(payload, resp_payload)

    def test_02_future_wait_for(self):
        payload = {"seq": 1, "pad": "stuff"}
        future = self.f.rpc("cmb.ping", payload)
        try:
            future.wait_for(5)
            resp_payload = future.get()
        except EnvironmentError as e:
            if e.errno == errno.ETIMEDOUT:
                self.fail(msg="future fulfillment timed out")
            else:
                raise
        self.assertDictContainsSubset(payload, resp_payload)

    def test_03_future_then(self):
        """Register a 'then' cb and run the reactor to ensure it runs"""
        cb_ran = [False]

        def then_cb(future, arg):
            flux_handle = future.get_flux()
            reactor = future.get_reactor()
            try:
                resp_payload = future.get()
                cb_ran[0] = True
                self.assertDictContainsSubset(arg, resp_payload)
            finally:
                # ensure that reactor is always stopped, avoiding a hung test
                flux_handle.reactor_stop(reactor)

        payload = {"seq": 1, "pad": "stuff"}
        self.f.rpc(b"cmb.ping", payload).then(then_cb, arg=payload)
        # force a full garbage collection pass to test that our anonymous RPC doesn't disappear
        gc.collect(2)
        ret = self.f.reactor_run(self.f.get_reactor(), 0)
        self.assertGreaterEqual(ret, 0, msg="Reactor exited with {}".format(ret))
        self.assertTrue(cb_ran[0], msg="Callback did not run successfully")

    def test_04_double_future_then(self):
        """Register two 'then' cbs and ensure it throws an exception"""
        with self.assertRaises(EnvironmentError) as cm:
            rpc = self.f.rpc(b"cmb.ping")
            rpc.then(lambda x, y: None)
            rpc.then(lambda x, y: None)
        self.assertEqual(cm.exception.errno, errno.EEXIST)

    def test_05_future_error_string(self):
        with self.assertRaises(EnvironmentError) as cm:
            payload = {"J": "", "priority": -1000, "flags": 0}
            future = self.f.rpc("job-ingest.submit", payload=payload)
            future.get()
        self.assertEqual(cm.exception.errno, errno.EINVAL)
        # Ensure that the result of flux_future_error_string propagated up
        self.assertEqual(cm.exception.strerror, future.error_string())
        self.assertRegexpMatches(cm.exception.strerror, "priority range is .*")

    def test_06_blocking_methods(self):
        future = Future(self.f.future_create(ffi.NULL, ffi.NULL))

        self.assertFalse(future.is_ready())
        with self.assertRaises(EnvironmentError) as cm:
            future.wait_for(timeout=0)
        self.assertEqual(cm.exception.errno, errno.ETIMEDOUT)

        future.pimpl.fulfill(ffi.NULL, ffi.NULL)
        self.assertTrue(future.is_ready())
        try:
            future.wait_for(0)
        except EnvironmentError as e:
            self.fail("future.wait_for raised an unexpected exception: {}".format(e))


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
