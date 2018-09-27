#!/usr/bin/env python
import unittest
import errno
import os
import sys
import flux.core as core
import flux
import flux.kvs
import json
import multiprocessing as mp

from six import print_ as print
from six.moves import range as range

import sideflux
from pycotap import TAPTestRunner

def barr_count(x, name, count):
  print(proc, x)
  f = core.Flux()
  f.barrier(name,count)

class TestBarrier(unittest.TestCase):
    def setUp(self):
        """Create a handle, connect to flux"""
        self.sf = sideflux.SideFlux(8)
        self.sf.start()
        self.f = core.Flux(self.sf.flux_uri)

    def tearDown(self):
        self.sf.destroy()

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
      unittest.main(testRunner=TAPTestRunner())

