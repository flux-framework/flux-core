#!/usr/bin/env python
from __future__ import print_function
import unittest
import multiprocessing as mp

from six.moves import range as range

import flux
from subflux import rerun_under_flux


def barr_count(x, name, count):
    print(x, name, count)
    f = flux.Flux()
    f.barrier(name, count)
    f.close()


def __flux_size():
    return 8


class TestBarrier(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    @classmethod
    def tearDownClass(self):
        self.f.close()

    def test_single(self):
        self.f.barrier("testbarrier1", 1)
        self.f.barrier(u"testbarrier1", 1)

    def test_eight(self):
        for topic in [b"testbarrier2", u"\xa3", u"\u32db \u263a \u32e1"]:
            for i in range(1, 9):
                p = mp.Pool(i)
                reslist = []
                for j in range(0, i):
                    res = p.apply_async(barr_count, (j, topic, i))
                    reslist.append(res)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
