#!/usr/bin/env python
from __future__ import print_function
import unittest
import multiprocessing as mp

from six.moves import range as range

import flux.core as core
from subflux import rerun_under_flux

def barr_count(x, name, count):
    print(x, name, count)
    f = core.Flux()
    f.barrier(name,count)

def __flux_size():
    return 8

class TestBarrier(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = core.Flux()

    @classmethod
    def tearDownClass(self):
        self.f.close()

    def test_single(self):
        self.f.barrier('testbarrier1', 1)

    def test_eight(self):
      for i in range(1,9):
        p = mp.Pool(i)
        reslist = []
        for j in range(0, i):
          res = p.apply_async(barr_count, (j, 'testbarrier2', i))
          reslist.append(res)

if __name__ == '__main__':
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner
        unittest.main(testRunner=TAPTestRunner())
