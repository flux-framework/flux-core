#!/usr/bin/env python3
###############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
import unittest

import flux
from flux.job import JobspecV1, JournalConsumer
from subflux import rerun_under_flux


def __flux_size():
    return 2


def waitall(flux_handle):
    while True:
        try:
            flux.job.wait(flux_handle)
        except OSError as exc:
            if exc.errno == errno.ECHILD:
                # No more jobs to wait for
                break


class TestJournalConsumer(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

        # Run a couple jobs for historical data
        jobspec = JobspecV1.from_command(["sleep", "0"])
        for i in range(4):
            flux.job.submit(self.fh, jobspec, waitable=True)
        waitall(self.fh)

    def test_history(self):
        consumer = JournalConsumer(self.fh).start()

        count = 0
        events = []
        while True:
            # Read events until we get 4 clean events
            event = consumer.poll(timeout=5.0)
            events.append(event)
            if event.name == "clean":
                count += 1
                if count == 4:
                    break
        consumer.stop()

        # events are ordered
        self.assertTrue(
            all(
                events[index].timestamp < events[index + 1].timestamp
                for index in range(len(events) - 1)
            )
        )

        # get timestamp of arbitrary event in history:
        seq = 10
        timestamp = events[seq].timestamp

        # new consumer with since timestamp
        consumer = JournalConsumer(self.fh, since=timestamp).start()
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
            if event.name == "clean":
                count[0] += 1
                if count[0] == 4:
                    self.fh.reactor_stop()

        consumer = JournalConsumer(self.fh).start()
        consumer.set_callback(test_cb, test_arg2, kw_arg=test_kw_arg)

        self.fh.reactor_run()

        self.assertEqual(count[0], 4)

        # historical events are ordered
        self.assertTrue(
            all(
                events[index].timestamp < events[index + 1].timestamp
                for index in range(len(events) - 1)
            )
        )

        new_events = []

        def new_cb(event):
            if event is None:
                # End of stream
                self.fh.reactor_stop()
            new_events.append(event)
            if event.name == "clean":
                consumer.stop()

        # Reset callback to append to new_events
        consumer.set_callback(new_cb)

        jobid = flux.job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))

        self.fh.reactor_run()

        for event in new_events:
            self.assertTrue(event.jobid == jobid)

    def test_nonfull(self):
        consumer = flux.job.JournalConsumer(self.fh, full=False).start()

        events = []

        def cb(event):
            if event is None:
                self.fh.reactor_stop()
                return
            events.append(event)
            if event.name == "clean":
                consumer.stop()

        consumer.set_callback(cb)

        jobid = flux.job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))

        self.fh.reactor_run()

        for event in events:
            self.assertTrue(event.jobid == jobid)

    def test_sentinel(self):
        self.assertTrue(flux.job.journal.SENTINEL_EVENT.is_empty())
        # Ensure empty event can be converted to a string:
        print(flux.job.journal.SENTINEL_EVENT)

        consumer = flux.job.JournalConsumer(self.fh, include_sentinel=True)
        consumer.start()
        while True:
            event = consumer.poll(0.1)
            if event == flux.job.journal.SENTINEL_EVENT and event.is_empty():
                break

    def test_sentinel_async(self):

        def cb(event):
            if event is None:
                self.fh.reactor_stop()
            elif event.is_empty():
                consumer.stop()

        consumer = flux.job.JournalConsumer(self.fh, include_sentinel=True)
        consumer.start()
        consumer.set_callback(cb)
        self.fh.reactor_run()

    def test_poll_timeout(self):

        consumer = flux.job.JournalConsumer(self.fh, full=False).start()
        with self.assertRaises(TimeoutError):
            consumer.poll(timeout=0.1)
        consumer.stop()

    def test_poll_ENODATA(self):

        consumer = flux.job.JournalConsumer(self.fh, full=False).start()
        consumer.stop()
        self.assertIsNone(consumer.poll(timeout=5.0))


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)
