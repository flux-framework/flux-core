#!/usr/bin/env python
import unittest
import errno
import os
import sys
import flux.core as core
import flux
import flux.kvs
import json
import syslog
from pycotap import TAPTestRunner
from sideflux import run_beside_flux

def __flux_size():
  return 2

class TestHandle(unittest.TestCase):
    def setUp(self):
        """Create a handle, connect to flux"""
        self.f = core.Flux()

    def test_create_handle(self):
        """Successfully connected to flux"""
        self.assertIsNotNone(self.f)

    def test_log(self):
        """Successfully connected to flux"""
        self.f.log(syslog.LOG_INFO, 'hi')

    def test_rpc_ping(self):
        """Sending a ping"""
        r = self.f.rpc_send('cmb.ping', {'seq': 1, 'pad': 'stuff'})
        self.assertEqual(r['seq'], 1)
        self.assertEqual(r['pad'], 'stuff')

    def test_rpc_with(self):
        """Sending a ping"""
        with self.f.rpc_create('cmb.ping', {'seq': 1, 'pad': 'stuff'}) as r:
            j = r.get()
            self.assertEqual(j['seq'], 1)
            self.assertEqual(j['pad'], 'stuff')

    def test_get_rank(self):
      """Get flux rank"""
      rank = self.f.get_rank()
      self.assertEqual(rank, 0)
