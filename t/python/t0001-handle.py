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


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
