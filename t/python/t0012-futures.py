#!/usr/bin/env python3

###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
import gc
import unittest

import flux
import flux.constants
from flux.core.inner import ffi
from flux.future import Future, FutureExt
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestHandle(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        """Create a handle, connect to flux"""
        self.f = flux.Flux()
        self.ping_payload = {"seq": 1, "pad": "stuff"}

    def test_01_rpc_get(self):
        future = self.f.rpc("broker.ping", self.ping_payload)
        resp_payload = future.get()
        self.assertDictContainsSubset(self.ping_payload, resp_payload)

    def test_02_get_flux(self):
        future = self.f.rpc("broker.ping", self.ping_payload)
        future.get_flux()
        # force a full garbage collection pass to test that the handle is not destructed
        gc.collect(2)
        resp_payload = future.get()
        self.assertDictContainsSubset(self.ping_payload, resp_payload)

    def test_02_future_wait_for(self):
        future = self.f.rpc("broker.ping", self.ping_payload)
        try:
            future.wait_for(5)
            resp_payload = future.get()
        except EnvironmentError as e:
            if e.errno == errno.ETIMEDOUT:
                self.fail(msg="future fulfillment timed out")
            else:
                raise
        self.assertDictContainsSubset(self.ping_payload, resp_payload)

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

        self.f.rpc(b"broker.ping", self.ping_payload).then(
            then_cb, arg=self.ping_payload
        )
        # force a full garbage collection pass to test that our anonymous RPC doesn't disappear
        gc.collect(2)
        ret = self.f.reactor_run()
        self.assertGreaterEqual(ret, 0, msg="Reactor exited with {}".format(ret))
        self.assertTrue(cb_ran[0], msg="Callback did not run successfully")

    def test_03_future_then_exception(self):
        def then_cb(future):
            raise RuntimeError("this is a test")

        self.f.rpc("broker.ping", self.ping_payload).then(then_cb)
        with self.assertRaises(RuntimeError):
            self.f.reactor_run()

    def test_03_future_then_varargs(self):
        cb_ran = [False]

        def then_cb(future, one, two, three):
            cb_ran[0] = True
            try:
                self.assertEqual(one, "one")
                self.assertEqual(two, "two")
                self.assertEqual(three, "three")
            finally:
                future.get_flux().reactor_stop()

        self.f.rpc("broker.ping").then(then_cb, "one", "two", "three")
        gc.collect(2)
        ret = self.f.reactor_run()
        self.assertGreaterEqual(ret, 0, msg="Reactor exited with < 0")
        self.assertTrue(cb_ran[0], msg="Callback did not run successfully")

    def test_03_future_then_noargs(self):
        cb_ran = [False]

        def then_cb(future):
            cb_ran[0] = True
            future.get_flux().reactor_stop()

        self.f.rpc("broker.ping").then(then_cb)
        gc.collect(2)
        ret = self.f.reactor_run()
        self.assertGreaterEqual(ret, 0, msg="Reactor exited with < 0")
        self.assertTrue(cb_ran[0], msg="Callback did not run successfully")

    def test_03_future_then_default_args(self):
        cb_ran = [False]

        def then_cb(future, args=None):
            cb_ran[0] = True
            try:
                self.assertIsNone(args)
            finally:
                future.get_flux().reactor_stop()

        self.f.rpc("broker.ping").then(then_cb)
        gc.collect(2)
        ret = self.f.reactor_run()
        self.assertGreaterEqual(ret, 0, msg="Reactor exited with < 0")
        self.assertTrue(cb_ran[0], msg="Callback did not run successfully")

    def test_03_future_then_kwargs(self):
        cb_ran = [False]

        def then_cb(future, val1=None, val2=None, val3="default"):
            cb_ran[0] = True
            try:
                self.assertTrue(val1)
                self.assertTrue(val2)
                # val3 gets default value
                self.assertEqual(val3, "default")
            finally:
                future.get_flux().reactor_stop()

        self.f.rpc("broker.ping").then(then_cb, val2=True, val1=True)
        gc.collect(2)
        ret = self.f.reactor_run()
        self.assertGreaterEqual(ret, 0, msg="Reactor exited with < 0")
        self.assertTrue(cb_ran[0], msg="Callback did not run successfully")

    def test_04_double_future_then(self):
        """Register two 'then' cbs and ensure it throws an exception"""
        with self.assertRaises(EnvironmentError) as cm:
            rpc = self.f.rpc(b"broker.ping")
            rpc.then(lambda x: None)
            rpc.then(lambda x: None)
        self.assertEqual(cm.exception.errno, errno.EEXIST)

    def test_05_future_error_string(self):
        with self.assertRaises(EnvironmentError) as cm:
            payload = {"J": "", "urgency": -1000, "flags": 0}
            future = self.f.rpc("job-ingest.submit", payload=payload)
            future.get()
        self.assertEqual(cm.exception.errno, errno.EINVAL)
        # Ensure that the result of flux_future_error_string propagated up
        self.assertEqual(cm.exception.strerror, future.error_string())
        self.assertRegexpMatches(cm.exception.strerror, "urgency range is .*")

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

    def test_07_streaming_rpcs(self):
        def continuation_cb(future, arg):
            arg["count"] += 1
            if arg["count"] >= arg["target"]:
                self.f.reactor_stop()
            future.reset()

        def service_cb(fh, t, msg, arg):
            for x in range(msg.payload["count"]):
                fh.respond(msg, {"seq": x})

        self.f.service_register("rpctest").get()
        watcher = self.f.msg_watcher_create(
            service_cb, flux.constants.FLUX_MSGTYPE_REQUEST, "rpctest.multi"
        )
        self.assertIsNotNone(watcher)
        watcher.start()

        arg = {"count": 0, "target": 3}
        self.f.rpc(
            "rpctest.multi",
            {"count": arg["target"]},
            flags=flux.constants.FLUX_RPC_STREAMING,
        ).then(continuation_cb, arg=arg)
        self.f.reactor_run()
        self.assertEqual(arg["count"], arg["target"])

        watcher.stop()
        watcher.destroy()
        fut = self.f.service_unregister("rpctest")
        self.assertEqual(self.f.future_get(fut, ffi.NULL), 0)

    def test_08_future_from_future(self):
        orig_fut = Future(self.f.future_create(ffi.NULL, ffi.NULL))
        new_fut = Future(orig_fut)
        self.assertFalse(new_fut.is_ready())
        orig_fut.pimpl.fulfill(ffi.NULL, ffi.NULL)
        self.assertTrue(new_fut.is_ready())

        orig_fut = self.f.rpc("broker.ping", payload=self.ping_payload)
        new_fut = Future(orig_fut)
        del orig_fut
        new_fut.get()
        # Future's `get` returns `None`, so just test that it is fulfilled
        self.assertTrue(new_fut.is_ready())

        orig_fut = self.f.rpc("foo.bar")
        new_fut = Future(orig_fut)
        del orig_fut
        with self.assertRaises(EnvironmentError):
            new_fut.get()

    def test_20_FutureExt(self):
        cb_ran = [False]

        def init_cb(future):
            cb_ran[0] = True
            self.assertIsInstance(future, Future)
            future.fulfill("foo")

        future = FutureExt(init_cb)
        self.assertIsInstance(future, FutureExt)
        self.assertFalse(cb_ran[0])
        self.assertEqual(future.get(), "foo")
        self.assertTrue(cb_ran[0])

        future.reset()
        future.fulfill("bar")
        self.assertEqual(future.get(), "bar")

        future.reset()
        future.fulfill({"key": "value"})
        self.assertDictEqual(future.get(), {"key": "value"})

        # fulfill with None
        future.reset()
        future.fulfill()
        self.assertEqual(future.get(), None)

        future.reset()
        future.fulfill_error(errno.EINVAL, "test error")
        with self.assertRaises(OSError):
            future.get()

    def test_21_FutureExt_complex(self):
        class Ping5(FutureExt):
            """Test future that returns after 5 pings"""

            def __init__(self, flux_handle):
                super().__init__(init_cb=self.init_cb, flux_handle=flux_handle)

            def ping_cb(self, future, original_future):
                seq = future.get()["seq"]
                if seq == 5:
                    try:
                        original_future.fulfill(future.get())
                    except Exception as exc:
                        print(exc)
                    return
                h = future.get_flux()
                h.rpc("broker.ping", {"seq": seq + 1}).then(
                    self.ping_cb, original_future
                )

            def init_cb(self, future):
                h = future.get_flux()
                h.rpc("broker.ping", {"seq": 1}).then(self.ping_cb, future)

        result = Ping5(self.f).get()
        self.assertEqual(result["seq"], 5)

        #  Now, try the same in async context:
        cb_ran = [False]

        def ping5_cb(future, a, b=None):
            self.assertEqual(future.get()["seq"], 5)
            self.assertEqual(a, "foo")
            self.assertEqual(b, "bar")
            cb_ran[0] = True
            # XXX: reactor doesn't exit without this for unknown reason
            # works standalone though...
            future.get_flux().reactor_stop()

        Ping5(self.f).then(ping5_cb, "foo", b="bar")
        self.f.reactor_run()
        self.assertTrue(cb_ran[0])


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
