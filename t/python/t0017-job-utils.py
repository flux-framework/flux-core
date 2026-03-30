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
import os
import signal
import tempfile
import unittest

import subflux  # noqa: F401 - sets up PYTHONPATH
from flux.job import BatchConfig
from flux.job._utils import decode_signal
from pycotap import TAPTestRunner


class TestBatchConfig(unittest.TestCase):
    def test_01_update_keyval(self):
        bc = BatchConfig()
        bc.update("resource.noverify=true")
        self.assertEqual(bc.config, {"resource": {"noverify": True}})

    def test_02_update_keyval_string_value(self):
        bc = BatchConfig()
        bc.update("system.job.name=myjob")
        self.assertEqual(bc.config["system"]["job"]["name"], "myjob")

    def test_03_update_multiline_json(self):
        data = '{"resource": {"noverify": true}}'
        bc = BatchConfig()
        bc.update(data + "\n")  # newline forces multiline path
        self.assertEqual(bc.config, {"resource": {"noverify": True}})

    def test_04_update_multiline_toml(self):
        data = "[resource]\nnoverify = true\n"
        bc = BatchConfig()
        bc.update(data)
        self.assertEqual(bc.config, {"resource": {"noverify": True}})

    def test_05_update_file_toml(self):
        data = "[resource]\nnoverify = true\n"
        with tempfile.NamedTemporaryFile(suffix=".toml", mode="w", delete=False) as fp:
            fp.write(data)
            path = fp.name
        try:
            bc = BatchConfig()
            bc.update(path)
            self.assertEqual(bc.config, {"resource": {"noverify": True}})
        finally:
            os.unlink(path)

    def test_06_update_file_json(self):
        data = json.dumps({"resource": {"noverify": True}})
        with tempfile.NamedTemporaryFile(suffix=".json", mode="w", delete=False) as fp:
            fp.write(data)
            path = fp.name
        try:
            bc = BatchConfig()
            bc.update(path)
            self.assertEqual(bc.config, {"resource": {"noverify": True}})
        finally:
            os.unlink(path)

    def test_07_update_chain_merges(self):
        bc = BatchConfig()
        bc.update("a=1")
        bc.update("b=2")
        self.assertEqual(bc.config, {"a": 1, "b": 2})

    def test_08_invalid_multiline_raises(self):
        bc = BatchConfig()
        with self.assertRaises(ValueError):
            bc.update("not valid json or toml\n\n")

    def test_09_get(self):
        bc = BatchConfig()
        bc.update("resource.noverify=true")
        self.assertTrue(bc.get("resource.noverify"))
        self.assertIsNone(bc.get("nonexistent.key"))
        self.assertEqual(bc.get("nonexistent.key", default="x"), "x")

    def test_10_independent_instances(self):
        # Verify that two BatchConfig instances don't share state
        bc1 = BatchConfig()
        bc2 = BatchConfig()
        bc1.update("a=1")
        self.assertIsNone(bc2.config)


class TestDecodeSignal(unittest.TestCase):
    def test_01_int_passthrough(self):
        self.assertEqual(decode_signal(10), 10)

    def test_02_numeric_string(self):
        self.assertEqual(decode_signal("10"), 10)

    def test_03_sigusr1_form(self):
        self.assertEqual(decode_signal("SIGUSR1"), signal.SIGUSR1)

    def test_04_short_form(self):
        self.assertEqual(decode_signal("USR1"), signal.SIGUSR1)

    def test_05_invalid_raises(self):
        with self.assertRaises(ValueError):
            decode_signal("NOTASIGNAL")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
