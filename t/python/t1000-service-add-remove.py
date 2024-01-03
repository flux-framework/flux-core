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

import errno
import os
import sys
import unittest

import flux
from flux.constants import FLUX_MSGTYPE_REQUEST, FLUX_MSGTYPE_RESPONSE
from flux.message import Message
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestServiceAddRemove(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    def test_001_register_service(self):
        rc = self.f.service_register("foo").get()
        self.assertEqual(rc, None)

    def test_001_register_owner_service(self):
        rc = self.f.service_register("owner").get()
        self.assertEqual(rc, None)

    def test_002_service_add_eexist(self):
        with self.assertRaises(EnvironmentError) as e:
            self.f.service_register("foo").get()
        self.assertEqual(e.exception.errno, errno.EEXIST)

    def test_003_add_service_message_watcher(self):
        def service_cb(f, t, msg, arg):
            rc = f.respond(msg, msg.payload_str)
            self.assertEqual(rc, 0)

        self.f.watcher = self.f.msg_watcher_create(
            service_cb,
            FLUX_MSGTYPE_REQUEST,
            "foo.echo",
            rolemask=flux.constants.FLUX_ROLE_ALL,
        )
        self.assertIsNotNone(self.f.watcher)
        self.f.watcher.start()

    def test_003_add_owner_message_watcher(self):
        def service_cb(f, t, msg, arg):
            rc = f.respond(msg, msg.payload_str)
            self.assertEqual(rc, 0)

        # Note: default rolemask for handle.msg_watcher_create()
        # should allow instance-owner only:
        self.f.watcher2 = self.f.msg_watcher_create(
            service_cb,
            FLUX_MSGTYPE_REQUEST,
            "owner.echo",
        )
        self.assertIsNotNone(self.f.watcher2)
        self.f.watcher2.start()

    def test_004_service_rpc(self):
        cb_called = [False]  # So that cb_called[0] is mutable in inner function
        p = {"test": "foo"}

        def cb(f, t, msg, arg):
            cb_called[0] = True
            self.assertEqual(msg.payload, p)
            f.reactor_stop()

        w2 = self.f.msg_watcher_create(cb, FLUX_MSGTYPE_RESPONSE, "foo.echo")
        w2.start()
        self.assertIsNotNone(w2, msg="msg_watcher_create response handler")

        m = Message()
        m.topic = "foo.echo"
        m.payload = p
        self.assertTrue(m is not None)
        ret = self.f.send(m)
        self.assertEqual(ret, 0)

        ret = self.f.reactor_run()
        self.assertTrue(ret >= 0)
        self.assertTrue(cb_called[0])
        w2.stop()
        w2.destroy()

    def send_user_request(self, topic, payload):
        m = Message()
        m.topic = topic
        m.payload = payload
        m.pimpl.set_rolemask(flux.constants.FLUX_ROLE_USER)
        m.pimpl.set_userid(os.getuid() + 1)
        self.assertTrue(m is not None)
        ret = self.f.send(m)
        self.assertEqual(ret, 0)

    def test_004_service_rpc_user(self):
        cb_called = [False]  # So that cb_called[0] is mutable in inner function
        p = {"test": "bar"}

        def cb(f, t, msg, arg):
            cb_called[0] = True
            self.assertEqual(msg.payload, p)
            f.reactor_stop()

        w2 = self.f.msg_watcher_create(cb, FLUX_MSGTYPE_RESPONSE, "foo.echo")
        w2.start()
        self.assertIsNotNone(w2, msg="msg_watcher_create response handler")

        self.send_user_request("foo.echo", p)

        ret = self.f.reactor_run()
        self.assertTrue(ret >= 0)
        self.assertTrue(cb_called[0])
        w2.stop()
        w2.destroy()

    def test_004_service_rpc_user_eperm(self):
        cb_called = [False]  # So that cb_called[0] is mutable in inner function
        p = {"test": "baz"}

        def cb(f, t, msg, arg):
            cb_called[0] = True
            with self.assertRaises(OSError) as exc:
                msgtype, topic, data = msg.decode()
            self.assertEqual(exc.exception.errno, errno.EPERM)
            f.reactor_stop()

        w2 = self.f.msg_watcher_create(cb, FLUX_MSGTYPE_RESPONSE, "owner.echo")
        w2.start()
        self.assertIsNotNone(w2, msg="msg_watcher_create response handler")

        self.send_user_request("owner.echo", p)

        ret = self.f.reactor_run()
        self.assertTrue(ret >= 0)
        self.assertTrue(cb_called[0])
        w2.stop()
        w2.destroy()

    def test_003_add_service_message_watcher_respond_error(self):
        def error_service_cb(f, t, msg, arg):
            rc = f.respond_error(msg, errno.EINVAL, "Test error response")
            self.assertEqual(rc, 0)

        self.f.error_watcher = self.f.msg_watcher_create(
            error_service_cb, FLUX_MSGTYPE_REQUEST, "foo.error"
        )
        self.assertIsNotNone(self.f.error_watcher)
        self.f.error_watcher.start()

    def test_004_service_rpc_error(self):
        cb_called = [False]  # So that cb_called[0] is mutable in inner function

        def then_cb(future):
            cb_called[0] = True
            with self.assertRaises(OSError):
                future.get()
            future.get_flux().reactor_stop()

        self.f.rpc("foo.error").then(then_cb)

        ret = self.f.reactor_run()
        self.assertTrue(ret >= 0)
        self.assertTrue(cb_called[0])

    def test_005_unregister_service(self):
        rc = self.f.service_unregister("foo").get()
        self.assertEqual(rc, None)

        rc = self.f.service_unregister("owner").get()
        self.assertEqual(rc, None)

        # done with message handler
        self.f.watcher.destroy()
        self.f.watcher2.destroy()
        self.f.error_watcher.destroy()

    def test_006_unregister_service_enoent(self):
        with self.assertRaises(EnvironmentError) as e:
            self.f.service_unregister("foo").get()
        self.assertEqual(e.exception.errno, errno.ENOENT)

    def test_007_service_rpc_enosys(self):
        fut = self.f.rpc("foo.echo")
        with self.assertRaises(EnvironmentError) as e:
            fut.get()
        self.assertEqual(e.exception.errno, errno.ENOSYS)

    def test_008_service_remove_on_disconnect(self):
        from multiprocessing import Process

        #  Add "baz" service in a different process and let the
        #   process exit to cause a disconnect.
        #   then, ensure "baz" service was removed.
        #
        def add_service_and_disconnect():
            h = flux.Flux()
            try:
                h.service_register("baz").get()
            except Exception():
                sys.exit(-1)
            sys.exit(0)

        p = Process(target=add_service_and_disconnect)
        p.start()
        p.join()
        self.assertEqual(p.exitcode, 0)

        #  Ensure no "baz" service remains
        fut = self.f.rpc("baz.echo")
        with self.assertRaises(EnvironmentError) as e:
            fut.get()
        self.assertEqual(e.exception.errno, errno.ENOSYS)

    def test_009_service_add_remove_eproto(self):
        fut = self.f.rpc("service.add")
        with self.assertRaises(EnvironmentError) as e:
            fut.get()
        self.assertEqual(e.exception.errno, errno.EPROTO)
        fut = self.f.rpc("service.remove")
        with self.assertRaises(EnvironmentError) as e:
            fut.get()
        self.assertEqual(e.exception.errno, errno.EPROTO)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
