#!/usr/bin/env python
import re
import unittest
import doctest
import sideflux
import os
import importlib

from pycotap import TAPTestRunner

if __name__ == '__main__':
    me = os.path.dirname(os.path.abspath(__file__))
    tests_dir = os.path.join( me, '../test')
    loader = unittest.TestLoader()
    tests = loader.discover(tests_dir, pattern='*.py')
    runner = TAPTestRunner()
    runner.run(tests)

    fluxdir = os.path.abspath(os.path.join( me,'../flux'))
    for f in os.listdir(fluxdir):
      m = re.match('([^_].*).py$', f)
      if m:
        print '#Running doctests in:', f
        suite = unittest.TestSuite()
        test_mod = importlib.import_module('flux.' + m.group(1))
        size = 1
        try:
          size = test_mod.__flux_size()
        except:
          pass
        try:
          suite.addTests(doctest.DocTestSuite(test_mod))
        except ValueError:
          continue
        with sideflux.run_beside_flux(size):
          runner.run(suite)

