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

import syslog
import unittest

import flux
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestHandle(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        """Create a handle, connect to flux"""
        self.f = flux.Flux()

    def test_create_handle(self):
        """Successfully connected to flux"""
        self.assertIsNotNone(self.f)

    def test_log(self):
        """Successfully connected to flux"""
        self.f.log(syslog.LOG_INFO, "hello")
        self.f.log(syslog.LOG_INFO, b"world")

    def test_rpc_ping(self):
        """Sending a ping"""
        # python 3 json doesn't support bytes, but python 2 will treat them as str (i.e., bytes)
        r = self.f.rpc(b"broker.ping", {"seq": 1, "pad": "stuff"}).get()
        self.assertEqual(r["seq"], 1)
        self.assertEqual(r["pad"], "stuff")
        self.assertTrue(isinstance(r["pad"], str))

    def test_anonymous_handle_rpc_ping(self):
        """Send a ping using an anonymous/unnamed flux handle"""
        r = flux.Flux().rpc(b"broker.ping", {"seq": 1, "pad": "stuff"}).get()
        self.assertIsNotNone(r)
        self.assertEqual(r["seq"], 1)
        self.assertEqual(r["pad"], "stuff")

    def test_rpc_ping_unicode(self):
        """Sending a ping"""
        r = self.f.rpc(
            "broker.ping", {"\xa3": "value", "key": "\u32db \u263a \u32e1"}
        ).get()
        self.assertEqual(r["\xa3"], "value")
        self.assertEqual(r["key"], "\u32db \u263a \u32e1")
        self.assertTrue(isinstance(r["key"], str))

    def test_rpc_with(self):
        """Sending a ping"""
        with self.f.rpc("broker.ping", {"seq": 1, "pad": "stuff"}) as r:
            j = r.get()
            self.assertEqual(j["seq"], 1)
            self.assertEqual(j["pad"], "stuff")

    def test_rpc_null_payload(self):
        """Sending a request that receives a NULL response"""
        resp = self.f.rpc(
            "attr.set", {"name": "attr-that-doesnt-exist", "value": "foo"}
        ).get()
        self.assertIsNone(resp)

    def test_get_rank(self):
        """Get flux rank"""
        rank = self.f.get_rank()
        self.assertEqual(rank, 0)

    def test_attr_get(self):
        local_uri = self.f.attr_get("local-uri")
        self.assertTrue(isinstance(local_uri, str))
        self.assertEqual(local_uri[:6], "local:")

        attr_rank = int(self.f.attr_get("rank"))
        rank = self.f.get_rank()
        self.assertEqual(attr_rank, rank)

    def test_attr_set(self):
        self.f.attr_set("foo", "bar")
        self.assertEqual(self.f.attr_get("foo"), "bar")

        self.f.attr_set("baz", str(1))
        self.assertEqual(self.f.attr_get("baz"), "1")

        self.f.attr_set("utf8-test", "ƒ Φ Ψ Ω Ö")
        self.assertEqual(self.f.attr_get("utf8-test"), "ƒ Φ Ψ Ω Ö")

        with self.assertRaises(OSError):
            self.f.attr_set("local-uri", "foo")
        with self.assertRaises(ValueError):
            self.f.attr_set("foo", 1)
        with self.assertRaises(ValueError):
            self.f.attr_set(42, "foo")

    def test_conf_get(self):
        # Works with empty config
        self.assertEqual(self.f.conf_get(), {})

        # load test config
        testconf = {"a": {"b": {"c": 42}, "foo": "bar"}}
        self.f.rpc("config.load", testconf).get()

        # conf_get() still returns old config
        self.assertEqual(self.f.conf_get(), {})

        # conf_get() with update=True returns new config
        self.assertEqual(self.f.conf_get(update=True), testconf)

        # conf_get() with key works
        self.assertEqual(self.f.conf_get("a"), testconf["a"])
        self.assertEqual(self.f.conf_get("a.b"), testconf["a"]["b"])
        self.assertEqual(self.f.conf_get("a.b.c"), testconf["a"]["b"]["c"])
        self.assertEqual(self.f.conf_get("a.foo"), testconf["a"]["foo"])

        # conf_get() works with default
        self.assertIsNone(self.f.conf_get("a.baz"))
        self.assertEqual(self.f.conf_get("a.baz", default=42), 42)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
