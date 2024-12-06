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

import unittest

import subflux  # noqa: F401
from flux.conf_builtin import conf_builtin_get
from pycotap import TAPTestRunner


class TestConfBuiltin(unittest.TestCase):

    def test_conf_builtin_get(self):
        with self.assertRaises(ValueError):
            conf_builtin_get("foo")

        with self.assertRaises(ValueError):
            conf_builtin_get("confdir", which="badarg")

        self.assertIsNotNone(conf_builtin_get("confdir"))
        self.assertIsNotNone(conf_builtin_get("confdir", which="intree"))
        self.assertIsNotNone(conf_builtin_get("confdir", which="installed"))


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
