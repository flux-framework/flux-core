#!/usr/bin/env python
from __future__ import print_function
import re
import unittest
import doctest
import sideflux
import os
import importlib
import sys
import multiprocessing

import pycotap as tap


def run_tests_with_size(result_set, tests, size):
    if size > 0:
        with sideflux.run_beside_flux(size):
            tests(result_set)
    else:
        tests(result_set)


def run_under_dir(result_set, path, mod_prefix, pattern='([^_].*).py$'):
    me = os.path.dirname(os.path.abspath(__file__))
    fluxdir = os.path.abspath(os.path.join(me, path))
    for f in os.listdir(fluxdir):
        m = re.match(pattern, f)
        if m:
            test_mod = importlib.import_module('.'.join(
                (mod_prefix, m.group(1))))
            size = 0
            try:
                size = test_mod.__flux_size()
            except:
                pass

            # load unittest items
            suite = unittest.defaultTestLoader.loadTestsFromModule(test_mod)
            try:
                # add doctests
                suite.addTests(doctest.DocTestSuite(test_mod))
            except ValueError:
                pass
            if suite.countTestCases() <= 0:
                continue
            print('#Running doctests and unit tests in:', fluxdir + '/' + f)
            run_tests_with_size(result_set, suite, size)


if __name__ == '__main__':
    result_set = tap.TAPTestResult(
        sys.stdout, sys.stderr, tap.LogMode.LogToYAML,
        tap.LogMode.LogToDiagnostics, )
    run_under_dir(result_set, '../test', 'test')
    run_under_dir(result_set, '../flux', 'flux')
    result_set.printErrors()

    sys.exit(not result_set.wasSuccessful() )
