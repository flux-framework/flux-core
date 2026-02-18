#!/usr/bin/env python3
###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import flux
from flux.resource import ResourceJournalConsumer, ResourceSet
from subflux import rerun_under_flux


def __flux_size():
    return 4


class TestResourceJournalConsumer(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

    def test_history(self):
        consumer = ResourceJournalConsumer(self.fh).start()

        events = []
        while True:
            event = consumer.poll(timeout=5.0)
            events.append(event)
            # The following `since=` test assumes at least 3 events in
            # the events array. Ensure we've got that and exit this loop:
            if len(events) == 3:
                break
        consumer.stop()

        self.assertTrue(len(events) >= 3)

        # events are ordered
        self.assertTrue(
            all(
                events[index].timestamp < events[index + 1].timestamp
                for index in range(len(events) - 1)
            )
        )

        # get timestamp of arbitrary event in history:
        seq = 1
        timestamp = events[seq].timestamp

        # new consumer with since timestamp
        consumer = ResourceJournalConsumer(self.fh, since=timestamp).start()
        events2 = []
        while True:
            event = consumer.poll(timeout=5.0)
            events2.append(event)
            if event.timestamp == events[-1].timestamp:
                break

        consumer.stop()

        # events2 should be equal to the events[seq+1:]:
        self.assertListEqual(events2, events[seq + 1 :])

        # ensure JournalEvent can be converted to a string:
        print(f"{events[-1]}")

    def test_async(self):
        events = []
        test_arg2 = 42
        test_kw_arg = "foo"

        count = [0]

        def test_cb(event, arg2, kw_arg=None):
            events.append(event)
            self.assertEqual(arg2, test_arg2)
            self.assertEqual(kw_arg, test_kw_arg)
            count[0] += 1
            if count[0] == 4:
                # do not stop consumer here. Adding a new callback on
                # an active consumer is tested below
                self.fh.reactor_stop()

        consumer = ResourceJournalConsumer(self.fh).start()
        consumer.set_callback(test_cb, test_arg2, kw_arg=test_kw_arg)

        self.fh.reactor_run()

        # historical events are ordered
        self.assertTrue(
            all(
                events[index].timestamp < events[index + 1].timestamp
                for index in range(len(events) - 1)
            )
        )

        new_events = []

        def new_cb(event):
            if event.name == "drain":
                consumer.stop()
                self.fh.reactor_stop()
            new_events.append(event)

        # Reset callback to append to new_events
        consumer.set_callback(new_cb)

        # drain a node to append a new event to resource eventlog
        self.fh.rpc("resource.drain", {"targets": "0", "reason": "test"}).get()
        self.fh.reactor_run()

        for event in new_events:
            self.assertTrue(event.name == "drain")

        # restore state
        self.fh.rpc("resource.undrain", {"targets": "0"}).get()

        # stop consumer
        consumer.stop()

    def test_sentinel(self):
        self.assertTrue(ResourceJournalConsumer.SENTINEL_EVENT.is_empty())

        # Ensure empty event can be converted to a string:
        print(ResourceJournalConsumer.SENTINEL_EVENT)

        consumer = ResourceJournalConsumer(self.fh, include_sentinel=True)
        consumer.start()
        while True:
            event = consumer.poll(0.1)
            if event == consumer.SENTINEL_EVENT:
                break

    def test_sentinel_async(self):

        def cb(event):
            if event is None:
                self.fh.reactor_stop()
            if event.is_empty():
                consumer.stop()

        consumer = ResourceJournalConsumer(self.fh, include_sentinel=True)
        consumer.start()
        consumer.set_callback(cb)
        self.fh.reactor_run()

    def test_poll_timeout(self):

        consumer = ResourceJournalConsumer(self.fh).start()
        with self.assertRaises(TimeoutError):
            while True:
                consumer.poll(timeout=0.1)
        consumer.stop()

    def test_poll_ENODATA(self):

        consumer = ResourceJournalConsumer(self.fh, include_sentinel=True).start()
        while True:
            event = consumer.poll(0.1)
            if event.is_empty():
                consumer.stop()
                break
        self.assertIsNone(consumer.poll(timeout=5.0))

    def test_poll_RuntimeError(self):

        with self.assertRaises(RuntimeError):
            ResourceJournalConsumer(self.fh).poll(5.0)

    def test_poll_cb_set_before_start(self):

        def cb(event):
            if event.is_empty():
                consumer.stop()
                self.fh.reactor_stop()

        consumer = ResourceJournalConsumer(self.fh, include_sentinel=True)
        consumer.set_callback(cb)
        consumer.start()
        self.fh.reactor_run()

    def test_poll_cb_reset(self):
        """test that the consumer callback can be reset"""

        def cb(event):
            self.fail("incorrect callback called")

        def cb2(event):
            if event is None:
                self.fh.reactor_stop()
            if event.is_empty():
                consumer.stop()

        consumer = ResourceJournalConsumer(self.fh, include_sentinel=True)
        consumer.set_callback(cb)
        consumer.start()
        consumer.set_callback(cb2)
        self.fh.reactor_run()

    def test_R_in_resource_define_event(self):
        """test that R is available in resource-define event"""
        consumer = ResourceJournalConsumer(self.fh).start()
        while True:
            event = consumer.poll()
            if event is None:
                break
            if event.name == "resource-define":
                self.assertTrue(hasattr(event, "R"))
                R = ResourceSet(event.R)
                self.assertEqual(R.nnodes, 4)
                consumer.stop()

    def test_event_after_history(self):
        """Test that a consumer gets new events after history is processed"""

        def cb(event, wait):
            if event.is_empty():
                # history processed, now drain a rank
                print("got sentinel event, draining rank 0")
                self.fh.rpc(
                    "resource.drain", {"targets": "0", "reason": "history-test"}
                ).get()
            elif (
                event.name == "drain"
                and "reason" in event.context
                and event.context["reason"] == "history-test"
            ):
                print("got drain event, undraining rank 0")
                self.fh.rpc("resource.undrain", {"targets": "0"}).get()
                wait[0] = True
            elif wait[0] and event.name == "undrain":
                print("got undrain event, stopping consumer")
                consumer.stop()

        consumer = ResourceJournalConsumer(self.fh, include_sentinel=True)
        consumer.set_callback(cb, wait=[False])
        consumer.start()
        self.fh.reactor_run()


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)
