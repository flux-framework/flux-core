#!/usr/bin/env python3

###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import platform
import unittest

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.uri import JobURI
from pycotap import TAPTestRunner


class TestJobURI(unittest.TestCase):
    def test_parse_remote(self):
        uri = JobURI("ssh://foo.com/tmp/foo?tag=bar&x=y")
        self.assertEqual(uri.uri, "ssh://foo.com/tmp/foo?tag=bar&x=y")
        self.assertEqual(uri.remote, "ssh://foo.com/tmp/foo?tag=bar&x=y")
        self.assertEqual(uri.local, "local:///tmp/foo")
        self.assertEqual(uri.scheme, "ssh")
        self.assertEqual(uri.netloc, "foo.com")
        self.assertEqual(uri.path, "/tmp/foo")
        self.assertEqual(uri.query, "tag=bar&x=y")
        self.assertEqual(uri.fragment, "")
        self.assertEqual(uri.params, "")

    def test_parse_local(self):
        hostname = platform.uname()[1]
        uri = JobURI("local:///tmp/foo")
        self.assertEqual(uri.uri, "local:///tmp/foo")
        self.assertEqual(str(uri), "local:///tmp/foo")
        self.assertEqual(uri.remote, f"ssh://{hostname}/tmp/foo")
        self.assertEqual(uri.local, "local:///tmp/foo")
        self.assertEqual(uri.scheme, "local")
        self.assertEqual(uri.netloc, "")
        self.assertEqual(uri.path, "/tmp/foo")
        self.assertEqual(uri.query, "")
        self.assertEqual(uri.fragment, "")
        self.assertEqual(uri.params, "")

    def test_parse_local_with_remote_hostname(self):
        hostname = "fakehost"
        uri = JobURI("local:///tmp/foo", remote_hostname=hostname)
        self.assertEqual(uri.uri, "local:///tmp/foo")
        self.assertEqual(str(uri), "local:///tmp/foo")
        self.assertEqual(uri.remote, f"ssh://{hostname}/tmp/foo")
        self.assertEqual(uri.local, "local:///tmp/foo")
        self.assertEqual(uri.scheme, "local")
        self.assertEqual(uri.netloc, "")
        self.assertEqual(uri.path, "/tmp/foo")
        self.assertEqual(uri.query, "")
        self.assertEqual(uri.fragment, "")
        self.assertEqual(uri.params, "")

    def test_parse_errors(self):
        with self.assertRaises(ValueError):
            JobURI("foo:///tmp/bar").remote
        with self.assertRaises(ValueError):
            JobURI("foo:///tmp/bar").local
        with self.assertRaises(ValueError):
            JobURI("")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
