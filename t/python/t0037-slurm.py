#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
#
# flux.slurm module tests
#

import os
import tempfile
import time
import unittest
from pathlib import Path

import flux.slurm as slurm
import subflux  # noqa: F401 - To set up PYTHONPATH
from pycotap import TAPTestRunner


class TestSlurm(unittest.TestCase):
    """Tests for slurm._parse_slurm_time function"""

    @classmethod
    def setUpClass(cls):
        cls.orig_path = os.environ["PATH"]
        cls.t_path = str(Path(__file__).resolve().parent.parent / "scripts")
        os.environ["PATH"] = cls.t_path + ":" + os.environ["PATH"]

    def reset_path(self):
        os.environ["PATH"] = self.orig_path

    def test_parse_slurm_time(self):
        """Test squeue %L time parser"""
        tests = [
            {"input": "1:00:00", "result": 3600},
            {"input": "0:59", "result": 59},
            {"input": "10:00:00", "result": 36000},
            {"input": "1-00:00:00", "result": 86400},
            {"input": "1:01:04", "result": 3664},
            {"input": "11:00:00", "result": 39600},
            {"input": "11:23:59", "result": 41039},
            {"input": "12:00:00", "result": 43200},
            {"input": "1:21:32", "result": 4892},
            {"input": "16:00:00", "result": 57600},
            {"input": "2:00", "result": 120},
            {"input": "20:00", "result": 1200},
            {"input": "2-00:00:00", "result": 172800},
            {"input": "2-00:24:19", "result": 174259},
            {"input": "2-13:01:51", "result": 219711},
            {"input": "23:43:40", "result": 85420},
            {"input": "3-12:45:11", "result": 305111},
            {"input": "4-22:11:00", "result": 425460},
            {"input": "5:00", "result": 300},
            {"input": "59:13", "result": 3553},
            {"input": "7-00:00:00", "result": 604800},
            {"input": "0:00", "result": 0},
            {"input": "UNLIMITED", "result": None},
            {"input": "NOT_SET", "result": None},
            {"input": "a:bc", "result": None},
            {"input": "1:2:3:4:5", "result": None},
        ]
        for entry in tests:
            result = slurm._parse_slurm_time(entry["input"])
            self.assertEqual(result, entry["result"])

    def test_mock_remaining_time(self):

        with tempfile.NamedTemporaryFile(mode="w", delete=True) as fp:
            os.environ["FLUX_SLURM_MOCK_EXPIRATION_FILE"] = fp.name
            fp.write(str(int(time.time() + 3600)))
            fp.flush()
            self.assertAlmostEqual(slurm.slurm_timeleft(1234), 3600, delta=15)

            # Update expiration, timeleft should change
            fp.seek(0)
            fp.write(str(int(time.time() + 300)))
            fp.flush()
            self.assertAlmostEqual(slurm.slurm_timeleft(1234), 300, delta=15)

            # Update to unlimited
            fp.seek(0)
            fp.write(str(-1))
            fp.flush()
            self.assertIsNone(slurm.slurm_timeleft(1234))

            del os.environ["FLUX_SLURM_MOCK_EXPIRATION_FILE"]

    def test_squeue_missing(self):
        os.environ["PATH"] = "."
        try:
            with self.assertRaises(RuntimeError) as cm:
                slurm.slurm_timeleft(1234)
        finally:
            self.reset_path()
        self.assertRegex(str(cm.exception), "squeue not found")

    def test_squeue_failure(self):
        # FLUX_SLURM_MOCK_EXPIRATION_FILE not set, command will error
        with self.assertRaises(RuntimeError) as cm:
            slurm.slurm_timeleft(1234)
        self.assertRegex(str(cm.exception), "squeue failed")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner(), buffer=False)
