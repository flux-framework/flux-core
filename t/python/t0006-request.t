#!/usr/bin/env python
import unittest
import errno
import flux.core as core

json_str = '{"a":42}';
class TestRequestMethods(unittest.TestCase):

  def test_no_topic_invalid(self):
    """flux_request_encode returns EINVAL with no topic string"""
    f = core.Flux('loop://')
    with self.assertRaises(EnvironmentError) as err:
        f.request_encode(None, json_str)
    err = err.exception
    self.assertEqual(err.errno, errno.EINVAL)

  def test_null_payload(self):
    """flux_request_encode works with NULL payload"""
    f = core.Flux('loop://')
    self.assertTrue(
        f.request_encode("foo.bar", None) is not None
        )

if __name__ == '__main__':
  from pycotap import TAPTestRunner
  unittest.main(testRunner=TAPTestRunner())
