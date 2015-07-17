#!/usr/bin/env python
import re
import unittest
import doctest
import sideflux
import os
import importlib
import sys
import multiprocessing

from pycotap import TAPTestRunner


def applyable_run(tests):
    return TAPTestRunner().run(tests).wasSuccessful()


def run_tests_with_size(tests, size):
    if size > 0:
        return sideflux.apply_under_flux_async(size, applyable_run,
                                               (tests, )).get(timeout=20)
    else:
        return TAPTestRunner().run(tests)


def run_under_dir(path, mod_prefix, pattern='([^_].*).py$'):
    me = os.path.dirname(os.path.abspath(__file__))
    fluxdir = os.path.abspath(os.path.join(me, path))
    results = []
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
            print '#Running doctests and unit tests in:', fluxdir + '/' + f
            res = run_tests_with_size(suite, size)
            results.append(res)
    return results


if __name__ == '__main__':
    runners = []
    runners.extend(run_under_dir('../test', 'test'))
    runners.extend(run_under_dir('../flux', 'flux'))

    sys.exit(int(not all(runners)))
