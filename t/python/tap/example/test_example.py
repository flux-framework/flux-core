#!/usr/bin/env python
# pylint: disable=C0325

import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

import unittest
from pycotap import TAPTestRunner, LogMode

class MyTests(unittest.TestCase):
  def test_that_it_passes(self):
    print("First line of output")
    print("Second line of output")
    self.assertEqual(0, 0)

  @unittest.skip("Not finished yet")
  def test_that_it_skips(self):
    raise Exception("Does not happen")

  def test_that_it_fails(self):
    print("First line of output")
    print("Second line of output")
    self.assertEqual(1, 0)

if __name__ == '__main__':
  suite = unittest.TestLoader().loadTestsFromTestCase(MyTests)
  TAPTestRunner(message_log = LogMode.LogToYAML, test_output_log = LogMode.LogToDiagnostics).run(suite)
