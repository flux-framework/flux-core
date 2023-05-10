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

import unittest
import subflux  # To set up PYTHONPATH
from pycotap import TAPTestRunner
import signal
import flux.compat36


class TestCompat36(unittest.TestCase):
    def test_strsignal(self):
        # Cover getting all signals strings, sanity check
        # values of most common ones.
        # N.B. SIGSTKFLT not defined until Python 3.11, will get ValueError
        for i in range(signal.SIGHUP, signal.SIGIO):
            try:
                desc = flux.compat36.strsignal(i)
            except ValueError:
                pass

        self.assertEqual(flux.compat36.strsignal(signal.SIGHUP), "Hangup")
        self.assertEqual(flux.compat36.strsignal(signal.SIGINT), "Interrupt")
        self.assertEqual(flux.compat36.strsignal(signal.SIGKILL), "Killed")
        self.assertEqual(flux.compat36.strsignal(signal.SIGSEGV), "Segmentation Fault")
        self.assertEqual(flux.compat36.strsignal(signal.SIGTERM), "Terminated")

    def test_strsignal_invalid(self):
        gotvalueerror = False

        try:
            str = flux.compat36.strsignal(0)
        except ValueError:
            gotvalueerror = True

        self.assertIs(gotvalueerror, True)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
