#!/usr/bin/env python
import unittest
import errno
import os
import sys
import flux.core as core
import flux
import flux.kvs
import json
from pycotap import TAPTestRunner
from sideflux import run_beside_flux

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
        self.assertGreaterEqual(self.f.event_subscribe("testevent.1"), 0)
        self.assertGreaterEqual(self.f.event_unsubscribe("testevent.1"), 0)

    def test_full_event(self):
        """Subscribe send receive and unpack event"""
        self.assertGreaterEqual(self.f.event_subscribe("testevent.1"), 0)
        self.assertGreaterEqual(
            self.f.event_send("testevent.1", {'test': 'yay!'}), 0)
        evt = self.f.event_recv()
        self.assertIsNotNone(evt)
        self.assertEqual(evt.topic, 'testevent.1')
        pld = evt.payload
        self.assertIsNotNone(pld)
        self.assertEqual(pld['test'], 'yay!')
        self.assertIsNotNone(evt.payload_str)
        print evt.payload_str


