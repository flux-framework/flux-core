#!/usr/bin/env python
from __future__ import print_function
import unittest
import six

import flux.core as core
from subflux import rerun_under_flux

def __flux_size():
    return 2


class TestEvent(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        """Create a handle, connect to flux"""
        self.f = core.Flux()

    @classmethod
    def tearDownClass(self):
        self.f.close()

    def test_t1_0_sub(self):
        """Subscribe to an event"""
        self.assertGreaterEqual(self.f.event_subscribe("testevent.1"), 0)

    def test_t1_1_unsub(self):
        """Unsubscribe from an event"""
        self.assertGreaterEqual(self.f.event_subscribe("testevent.2"), 0)
        self.assertGreaterEqual(self.f.event_unsubscribe("testevent.2"), 0)
        with self.assertRaises(FileNotFoundError):
            self.f.event_unsubscribe("nonexistent.event")

    def test_full_event(self):
        """Subscribe send receive and unpack event"""
        event_names = [b'testevent.3',
                       u'\u32db \u263a \u32e1']
        for event_name in event_names:
            self.assertGreaterEqual(self.f.event_subscribe(event_name), 0)
            self.assertGreaterEqual(
                self.f.event_send(event_name, {'test': 'yay!'}), 0)
            evt = self.f.event_recv()
            self.assertIsNotNone(evt)

            if isinstance(event_name, six.binary_type):
                self.assertEqual(evt.topic, event_name.decode('utf-8'))
            else:
                self.assertEqual(evt.topic, event_name)

            pld = evt.payload
            self.assertIsNotNone(pld)
            self.assertEqual(pld['test'], 'yay!')
            self.assertIsNotNone(evt.payload_str)
            self.assertGreaterEqual(self.f.event_unsubscribe(event_name), 0)

if __name__ == '__main__':
    if rerun_under_flux(__flux_size()):
      from pycotap import TAPTestRunner
      unittest.main(testRunner=TAPTestRunner())
