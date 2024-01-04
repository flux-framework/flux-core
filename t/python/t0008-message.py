#!/usr/bin/env python3

###############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import unittest

import flux
from flux.message import Message
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestMessage(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()
        self.f.service_register("test").get()

        self.msg_handler = self.f.msg_watcher_create(
            lambda f, t, msg, arg: f.respond(msg),
            flux.constants.FLUX_MSGTYPE_REQUEST,
            "test.*",
        )
        self.msg_handler.start()

    @classmethod
    def tearDownClass(self):
        self.f.service_unregister("test").get()
        self.msg_handler.destroy()

    def test_basic_request(self):
        payload = {"test": "foo"}
        m = Message()
        self.assertIsNotNone(m)

        # default type is request
        self.assertEqual(m.type, flux.constants.FLUX_MSGTYPE_REQUEST)
        self.assertEqual(m.type_str, "request")

        m.topic = "test"
        self.assertEqual(m.topic, "test")

        m.payload_str = "blah"
        self.assertEqual(m.payload_str, "blah")

        m.payload = payload
        self.assertEqual(m.payload, payload)
        self.assertEqual(m.payload_str, json.dumps(payload))

        msgtype, topic, payload_str = m.decode()
        self.assertEqual(msgtype, flux.constants.FLUX_MSGTYPE_REQUEST)
        self.assertEqual(topic, "test")
        self.assertEqual(payload_str, json.dumps(payload))

        # self.type setter works
        m.type = flux.constants.FLUX_MSGTYPE_RESPONSE
        self.assertEqual(m.type, flux.constants.FLUX_MSGTYPE_RESPONSE)

    def test_basic_response(self):
        m = Message(flux.constants.FLUX_MSGTYPE_RESPONSE)
        self.assertIsNotNone(m)
        self.assertEqual(m.type, flux.constants.FLUX_MSGTYPE_RESPONSE)
        self.assertEqual(m.type_str, "response")

        m.topic = "test"
        self.assertEqual(m.topic, "test")

        msgtype, topic, payload_str = m.decode()
        self.assertEqual(msgtype, flux.constants.FLUX_MSGTYPE_RESPONSE)
        self.assertEqual(topic, "test")
        self.assertIsNone(payload_str)

    def test_baseic_event(self):
        m = Message(flux.constants.FLUX_MSGTYPE_EVENT)
        self.assertIsNotNone(m)
        self.assertEqual(m.type, flux.constants.FLUX_MSGTYPE_EVENT)
        self.assertEqual(m.type_str, "event")

        m.topic = "event.test"
        self.assertEqual(m.topic, "event.test")

        msgtype, topic, payload_str = m.decode()
        self.assertEqual(msgtype, flux.constants.FLUX_MSGTYPE_EVENT)
        self.assertEqual(topic, "event.test")
        self.assertIsNone(payload_str)

    def test_send(self):
        payload = {"test": "foo"}
        cb_called = [False]

        def cb(handle, watcher, msg, called):
            cb_called[0] = True
            msgtype, topic, payload = msg.decode()
            self.assertEqual(msgtype, flux.constants.FLUX_MSGTYPE_RESPONSE)
            self.assertEqual(topic, "test.foo")
            self.assertIsNone(payload)
            handle.reactor_stop()

        watcher = self.f.msg_watcher_create(
            cb,
            flux.constants.FLUX_MSGTYPE_RESPONSE,
            "test.*",
        )
        self.assertIsNotNone(watcher)
        watcher.start()

        m = Message()
        self.assertIsNotNone(m)

        # default type is request
        self.assertEqual(m.type, flux.constants.FLUX_MSGTYPE_REQUEST)
        self.assertEqual(m.type_str, "request")

        m.topic = "test.foo"
        self.assertEqual(m.topic, "test.foo")
        self.payload = payload
        self.f.send(m)

        rc = self.f.reactor_run()
        self.assertTrue(rc > 0)
        self.assertTrue(cb_called[0])

        watcher.destroy()


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
