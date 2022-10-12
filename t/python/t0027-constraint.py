#!/usr/bin/env python3
###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest
import unittest.mock
import platform
import subflux  # To set up PYTHONPATH
from pycotap import TAPTestRunner
from flux.job import Constraint


class TestConstraintValueError(unittest.TestCase):
    def __init__(self, arg):
        super().__init__()
        self.arg = arg

    def id(self):
        return f"TestConstraintValueError: {self.arg}"

    def runTest(self):
        with self.assertRaises(ValueError) as error:
            Constraint(self.arg)


class TestConstraint(unittest.TestCase):
    def __init__(self, arg, expected):
        super().__init__()
        self.arg = arg
        self.expected = expected

    def id(self):
        return f"TestConstraint: {self.arg}"

    def runTest(self):
        self.assertEqual(Constraint(self.arg), self.expected)


def suite():
    tests = {
        "foo": {"properties": ["foo"]},
        "(foo)": {"properties": ["foo"]},
        "foo,bar": {"properties": ["foo", "bar"]},
        "not foo,bar": {"not": [{"properties": ["foo", "bar"]}]},
        "hosts:foo[1-10]": {"hostlist": ["foo[1-10]"]},
        "foo and hosts:foo[1-10]": {
            "and": [{"properties": ["foo"]}, {"hostlist": ["foo[1-10]"]}]
        },
        "foo and -hosts:foo[1-10]": {
            "and": [{"properties": ["foo"]}, {"not": [{"hostlist": ["foo[1-10]"]}]}]
        },
        "foo or (not host:foo[1-10])": {
            "or": [{"properties": ["foo"]}, {"not": [{"hostlist": ["foo[1-10]"]}]}]
        },
    }
    suite = unittest.TestSuite()
    for arg, expected in tests.items():
        suite.addTest(TestConstraint(arg, expected))

    invalid = ["and", "foo not bar", "foo and (host:foo", "foo or"]
    for arg in invalid:
        suite.addTest(TestConstraintValueError(arg))

    return suite


if __name__ == "__main__":
    TAPTestRunner().run(suite())
