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
from pycotap import TAPTestRunner
from sideflux import run_beside_flux

def __flux_size():
  return 8

def test_barr_count(x, name, count):
  print proc, x
  f = core.Flux()
  f.barrier(name,count) 

class TestBarrier(unittest.TestCase):
    def setUp(self):
        """Create a handle, connect to flux"""
        self.f = core.Flux()

    def test_single(self):
        self.f.barrier('testbarrier1', 1)

    def test_eight(self):
      for i in xrange(1,9):
        p = mp.Pool(i)
        reslist = []
        for j in xrange(0, i):
          res = p.apply_async(test_barr_count, (j, 'testbarrier2', i))
          reslist.append(res)



