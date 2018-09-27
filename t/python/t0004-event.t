#!/usr/bin/env python
from __future__ import print_function
import unittest

import flux.core as core
from subflux import rerun_under_flux

def __flux_size():
  return 2

class TestEvent(unittest.TestCase):
    def setUp(self):
        """Create a handle, connect to flux"""
        self.f = core.Flux()

    def test_t1_0_sub(self):
        """Subscribe to an event"""
        self.assertGreaterEqual(self.f.event_subscribe("testevent.1"), 0)

    def test_t1_1_unsub(self):
        """Unsubscribe from an event"""
        self.assertGreaterEqual(self.f.event_subscribe("testevent.2"), 0)
        self.assertGreaterEqual(self.f.event_unsubscribe("testevent.2"), 0)

    def test_full_event(self):
        """Subscribe send receive and unpack event"""
        self.assertGreaterEqual(self.f.event_subscribe("testevent.1"), 0)
        self.assertGreaterEqual(
            self.f.event_send("testevent.1", {'test': 'yay!'}), 0)
        evt = self.f.event_recv()
        self.assertIsNotNone(evt)
        self.assertEqual(evt.topic, b'testevent.1')
        pld = evt.payload
        self.assertIsNotNone(pld)
        self.assertEqual(pld['test'], 'yay!')
        self.assertIsNotNone(evt.payload_str)
        print ( evt.payload_str )

if __name__ == '__main__':
    if rerun_under_flux(__flux_size()):
      from pycotap import TAPTestRunner
      unittest.main(testRunner=TAPTestRunner())
