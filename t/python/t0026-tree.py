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

import io
import os
import unittest
import unittest.mock

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.util import Tree
from pycotap import TAPTestRunner


class TestTree(unittest.TestCase):
    @unittest.mock.patch("sys.stdout", new_callable=io.StringIO)
    def assertTreeRender(
        self,
        tree,
        mock_stdout,
        style="box",
        level=None,
        skip_root=False,
        truncate=True,
        expected_output=None,
    ):
        tree.render(style=style, level=level, skip_root=skip_root, truncate=truncate)
        self.assertEqual(mock_stdout.getvalue(), expected_output)

    @classmethod
    def setUpClass(self):
        self.trees = []
        for combine in [True, False]:
            root = Tree("Root", combine_children=combine)
            child = Tree("flux", combine_children=combine)
            child.append("sleep")

            child2 = child.append("flux")
            child2.append("foo")
            child2.append("bar")

            child.append("sleep")
            child.append("sleep")

            root.append_tree(child)
            root.append("sleep")
            root.append("sleep")
            root.append("sleep")
            self.trees.append(root)

    def test_create_tree(self):
        for tree in self.trees:
            self.assertIsNotNone(tree)

    def test_render_not_combined(self):
        expected_output = """\
Root
├── flux
│   ├── sleep
│   ├── flux
│   │   ├── foo
│   │   └── bar
│   ├── sleep
│   └── sleep
├── sleep
├── sleep
└── sleep
"""
        self.assertTreeRender(self.trees[1], expected_output=expected_output)

    def test_render_combined(self):
        expected_output = """\
Root
├── flux
│   ├── 3*[sleep]
│   └── flux
│       ├── foo
│       └── bar
└── 3*[sleep]
"""
        self.assertTreeRender(self.trees[0], expected_output=expected_output)

    def test_render_skip_root(self):
        expected_output = """\
flux
├── 3*[sleep]
└── flux
    ├── foo
    └── bar
3*[sleep]
"""
        self.assertTreeRender(
            self.trees[0], skip_root=True, expected_output=expected_output
        )

    def test_render_level0(self):
        expected_output = """\
Root
"""
        self.assertTreeRender(self.trees[1], level=0, expected_output=expected_output)

    def test_render_level1(self):
        expected_output = """\
Root
├── flux
├── sleep
├── sleep
└── sleep
"""
        self.assertTreeRender(self.trees[1], level=1, expected_output=expected_output)

    def test_render_level2(self):
        expected_output = """\
Root
├── flux
│   ├── sleep
│   ├── flux
│   ├── sleep
│   └── sleep
├── sleep
├── sleep
└── sleep
"""
        self.assertTreeRender(self.trees[1], level=2, expected_output=expected_output)

    def test_render_style_compact(self):
        expected_output = """\
Root
├─flux
│ ├─3*[sleep]
│ └─flux
│   ├─foo
│   └─bar
└─3*[sleep]
"""
        self.assertTreeRender(
            self.trees[0], style="compact", expected_output=expected_output
        )

    def test_render_style_ascii(self):
        expected_output = """\
Root
|-- flux
|   |-- sleep
|   |-- flux
|   |   |-- foo
|   |   `-- bar
|   |-- sleep
|   `-- sleep
|-- sleep
|-- sleep
`-- sleep
"""
        self.assertTreeRender(
            self.trees[1], style="ascii", expected_output=expected_output
        )

    def test_render_truncate(self):
        expected_output = """\
Root
|-- flux
|   |-- sl+
|   |-- fl+
|   |   |-+
|   |   `-+
|   |-- sl+
|   `-- sl+
|-- sleep
|-- sleep
`-- sleep
"""
        columns = os.environ.get("COLUMNS")
        os.environ["COLUMNS"] = "11"
        self.assertTreeRender(
            self.trees[1], style="ascii", truncate=True, expected_output=expected_output
        )
        if columns:
            os.environ["COLUMNS"] = columns


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
