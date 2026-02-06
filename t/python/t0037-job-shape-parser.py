#!/usr/bin/env python3
###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.shape.parser import ShapeParser, ShapeSyntaxError
from pycotap import TAPTestRunner

VALID = [
    {
        "in": "slot=4/node",
        "out": [
            {
                "type": "slot",
                "count": 4,
                "label": "task",
                "with": [{"type": "node", "count": 1}],
            }
        ],
    },
    {
        "in": "slot=3-30/node",
        "out": [
            {
                "type": "slot",
                "count": "3-30",
                "label": "task",
                "with": [{"type": "node", "count": 1}],
            }
        ],
    },
    {
        "in": "slot=4{nodelevel}/node{-x}/socket=2+/core=4+",
        "out": [
            {
                "type": "slot",
                "count": 4,
                "label": "nodelevel",
                "with": [
                    {
                        "type": "node",
                        "count": 1,
                        "exclusive": False,
                        "with": [
                            {
                                "type": "socket",
                                "count": "2+",
                                "with": [{"type": "core", "count": "4+"}],
                            }
                        ],
                    }
                ],
            }
        ],
    },
    {
        "in": "slot=4{nodelevel}/node/socket=2/core=4",
        "out": [
            {
                "type": "slot",
                "count": 4,
                "label": "nodelevel",
                "with": [
                    {
                        "type": "node",
                        "count": 1,
                        "with": [
                            {
                                "type": "socket",
                                "count": 2,
                                "with": [{"type": "core", "count": 4}],
                            }
                        ],
                    }
                ],
            }
        ],
    },
    {
        "in": "[cluster/slot=2{ib}/node/[memory=4{unit:GB};ib10g];switch/slot=2{bicore}/node/core=2]",
        "out": [
            {
                "type": "cluster",
                "count": 1,
                "with": [
                    {
                        "type": "slot",
                        "count": 2,
                        "label": "ib",
                        "with": [
                            {
                                "type": "node",
                                "count": 1,
                                "with": [
                                    {"type": "memory", "count": 4, "unit": "GB"},
                                    {"type": "ib10g", "count": 1},
                                ],
                            }
                        ],
                    }
                ],
            },
            {
                "type": "switch",
                "count": 1,
                "with": [
                    {
                        "type": "slot",
                        "count": 2,
                        "label": "bicore",
                        "with": [
                            {
                                "type": "node",
                                "count": 1,
                                "with": [{"type": "core", "count": 2}],
                            }
                        ],
                    }
                ],
            },
        ],
    },
    {
        "in": "cluster=2/slot/node=1+{-x}/core=30",
        "out": [
            {
                "type": "cluster",
                "count": 2,
                "with": [
                    {
                        "type": "slot",
                        "count": 1,
                        "label": "task",
                        "with": [
                            {
                                "type": "node",
                                "count": "1+",
                                "exclusive": False,
                                "with": [{"type": "core", "count": 30}],
                            }
                        ],
                    }
                ],
            }
        ],
    },
    {
        "in": "slot=4,9,16,25/node",
        "out": [
            {
                "type": "slot",
                "count": "4,9,16,25",
                "label": "task",
                "with": [{"type": "node", "count": 1}],
            }
        ],
    },
    {
        "in": "slot=10/core=2",
        "out": [
            {
                "type": "slot",
                "count": 10,
                "label": "task",
                "with": [{"type": "core", "count": 2}],
            }
        ],
    },
    {
        "in": "node/[slot=10{read-db}/[core;memory=4{unit:GB}];slot{db}/[core=6;memory=24{unit:GB}]]",
        "out": [
            {
                "type": "node",
                "count": 1,
                "with": [
                    {
                        "type": "slot",
                        "count": 10,
                        "label": "read-db",
                        "with": [
                            {"type": "core", "count": 1},
                            {"type": "memory", "count": 4, "unit": "GB"},
                        ],
                    },
                    {
                        "type": "slot",
                        "count": 1,
                        "label": "db",
                        "with": [
                            {"type": "core", "count": 6},
                            {"type": "memory", "count": 24, "unit": "GB"},
                        ],
                    },
                ],
            }
        ],
    },
    {
        "in": "slot=10/[memory=2+{unit:GB};core]",
        "out": [
            {
                "type": "slot",
                "count": 10,
                "label": "task",
                "with": [
                    {"type": "memory", "count": "2+", "unit": "GB"},
                    {"type": "core", "count": 1},
                ],
            }
        ],
    },
    {
        "in": "slot=2{4GB-node}/node/memory=4+{unit:GB}",
        "out": [
            {
                "type": "slot",
                "count": 2,
                "label": "4GB-node",
                "with": [
                    {
                        "type": "node",
                        "count": 1,
                        "with": [{"type": "memory", "count": "4+", "unit": "GB"}],
                    }
                ],
            }
        ],
    },
    {
        "in": "slot/node",
        "out": [
            {
                "type": "slot",
                "count": 1,
                "label": "task",
                "with": [{"type": "node", "count": 1}],
            }
        ],
    },
]

INVALID = [
    "slot=2{blah",
    "node/[slot=10{read-db}/[core;memory=4{unit:GB}];slot{db}/[blah",
    "slot/",
    "slot=2{bla}}",
    "node/[slot=10{read-db}/[core;memory=4{unit:GB}]]]",
]


class TestParser(unittest.TestCase):
    def test_parse_valid(self):
        parser = ShapeParser()
        for test in VALID:
            print(f"checking `{test['in']}'")
            result = parser.parse(test["in"])
            self.assertEqual(test["out"], result)

    def test_parse_invalid(self):
        parser = ShapeParser()
        for test in INVALID:
            print(f"checking invalid input `{test}'")
            with self.assertRaises((SyntaxError, ShapeSyntaxError)):
                parser.parse(test)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
