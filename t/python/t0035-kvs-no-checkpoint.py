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

import errno
import unittest

import flux
import flux.constants
import flux.kvs
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestKVSNoCheckpoint(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    def test_kvs_no_checkpoint_01(self):
        with self.assertRaises(OSError) as cm:
            flux.kvs.kvs_checkpoint_lookup(self.f)
        self.assertEqual(cm.exception.errno, errno.ENOENT)

        with self.assertRaises(OSError) as cm:
            flux.kvs.kvs_checkpoint_lookup(self.f, cache_bypass=True)
        self.assertEqual(cm.exception.errno, errno.ENOENT)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size(), personality="content"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
