#!/usr/bin/env python

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from __future__ import print_function
import sys
import os
import errno
import json

import unittest

import flux


def _flux_size():
    return 4


script_dir = os.path.dirname(os.path.realpath(__file__))


class TestMRPC(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

    def test_00_mrpc_invalid_topic(self):
        with self.assertRaises(EnvironmentError) as error:
            ret = self.fh.mrpc_create(None)
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(TypeError):
            ret = self.fh.mrpc_create(5)

    def test_00_mrpc_invalid_rankset(self):
        with self.assertRaises(EnvironmentError) as error:
            ret = self.fh.mrpc_create("topic", {"foo": "bar"}, [])
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            ret = self.fh.mrpc_create("topic", {"foo": "bar"}, "foo")
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(TypeError) as error:
            ret = self.fh.mrpc_create("topic", {"foo": "bar"}, ["foo"])

    def test_01_mrpc_single(self):
        responses = list(self.fh.mrpc_create("cmb.ping", {"foo": "bar"}, [0]))
        self.assertEqual(len(responses), 1)
        nodeid, payload = responses[0]
        self.assertEqual(nodeid, 0)

    def test_02_mrpc_explicit_rankset(self):
        rankset = [0, 2]
        responses = sorted(
            self.fh.mrpc_create("cmb.ping", {"foo": "bar"}, rankset), key=lambda x: x[0]
        )
        self.assertEqual(len(responses), len(rankset))
        for rank, (nodeid, payload) in zip(rankset, responses):
            self.assertEqual(rank, nodeid)
            self.assertEqual(payload["foo"], "bar")

    def test_03_mrpc_all(self):
        self.assertGreaterEqual(_flux_size(), 2)
        # test both unicode and binary rankset shorthands
        for rankset in [b"all", u"all"]:
            responses = sorted(
                self.fh.mrpc_create(
                    "cmb.ping", payload={"foo": "bar"}, rankset=rankset
                ),
                key=lambda x: x[0],
            )
            self.assertEqual(len(responses), _flux_size())
            for rank, (nodeid, payload) in zip(range(_flux_size()), responses):
                self.assertEqual(rank, nodeid)
                self.assertEqual(payload["foo"], "bar")

    def test_04_mrpc_access_previous_payload(self):
        # test that payloads from the C API are copying and not invalidated
        # while iterating through responses
        self.assertGreaterEqual(_flux_size(), 2)
        prev_payloads = []
        for nodeid, payload in self.fh.mrpc_create("cmb.ping", {"foo": "bar"}, "all"):
            for prev_payload in prev_payloads:
                self.assertEqual(payload["foo"], prev_payload["foo"])
                self.assertItemsEqual(payload.keys(), prev_payload.keys())

    def test_05_unicode_args(self):
        binary_topic = b"cmb.ping"
        unicode_topic = u"cmb.ping"

        json_payload = {"foo": u"bar"}
        unicode_payload = json.dumps(json_payload, ensure_ascii=False)
        binary_payload = json.dumps(json_payload, ensure_ascii=False).encode("utf-8")

        for topic in [unicode_topic, binary_topic]:
            resp = self.fh.mrpc_create(
                topic=topic, payload=json_payload, rankset=[0]
            ).get()
            self.assertEqual(resp["foo"], u"bar")

        for payload in [json_payload, unicode_payload, binary_payload]:
            resp = self.fh.mrpc_create(binary_topic, payload=payload, rankset=[0]).get()
            self.assertEqual(resp["foo"], u"bar")

    def test_06_null_response_payload(self):
        resp = self.fh.mrpc_create(
            "attr.set", {"name": "attr-that-doesnt-exist", "value": "foo"}, rankset=[0]
        ).get()
        self.assertEqual(resp, None)


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=_flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
