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

# Unit tests for flux.proctitle.set_proctitle().
# No broker or real prctl() call is required; libc and platform are mocked.

import unittest
from unittest.mock import MagicMock, patch

import flux.proctitle as pt
import subflux  # noqa: F401 - To set up PYTHONPATH
from pycotap import TAPTestRunner


class TestSetProctitle(unittest.TestCase):
    def test_no_op_on_non_linux(self):
        """set_proctitle() returns without calling prctl on non-Linux platforms."""
        mock_libc = MagicMock()
        with patch.object(pt.platform, "system", return_value="Darwin"), patch.object(
            pt.ctypes, "CDLL", return_value=mock_libc
        ):
            pt.set_proctitle("test")
        mock_libc.prctl.assert_not_called()

    def test_prctl_called_on_linux(self):
        """set_proctitle() calls prctl(PR_SET_NAME) on Linux."""
        mock_libc = MagicMock()
        with patch.object(pt.platform, "system", return_value="Linux"), patch.object(
            pt.ctypes, "CDLL", return_value=mock_libc
        ):
            pt.set_proctitle("mymod")
        mock_libc.prctl.assert_called_once_with(15, b"mymod", 0, 0, 0)

    def test_prctl_failure_is_silently_ignored(self):
        """set_proctitle() does not raise when prctl() raises an exception."""
        mock_libc = MagicMock()
        mock_libc.prctl.side_effect = RuntimeError("mocked prctl failure")
        with patch.object(pt.platform, "system", return_value="Linux"), patch.object(
            pt.ctypes, "CDLL", return_value=mock_libc
        ):
            pt.set_proctitle("test")  # must not raise

    def test_cdll_failure_is_silently_ignored(self):
        """set_proctitle() does not raise when ctypes.CDLL() itself raises."""
        with patch.object(pt.platform, "system", return_value="Linux"), patch.object(
            pt.ctypes, "CDLL", side_effect=OSError("mocked CDLL failure")
        ):
            pt.set_proctitle("test")  # must not raise


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
