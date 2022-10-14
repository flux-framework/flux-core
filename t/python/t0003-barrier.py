#!/usr/bin/env python3

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest
import multiprocessing as mp

import flux
from subflux import rerun_under_flux


def barr_count(x, name, count):
    print(x, name, count)
    f = flux.Flux()
    f.barrier(name, count)


def __flux_size():
    return 8


class TestBarrier(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    def test_single(self):
        self.f.barrier("testbarrier1", 1)
        self.f.barrier("testbarrier1", 1)

    def test_eight(self):
        p = mp.Pool(8)
        for topic in [b"testbarrier2", "\xa3", "\u32db \u263a \u32e1"]:
            for i in range(1, 9):
                reslist = []
                for j in range(0, i):
                    res = p.apply_async(barr_count, (j, topic, i))
                    reslist.append(res)
                reslist[0].wait(10)  # timeout in 10 seconds if no success
                for r in reslist[1:]:  # wait for rest with short timeouts
                    r.get(0.5)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
