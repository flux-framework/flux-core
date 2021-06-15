#!/usr/bin/env python3

###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest
import collections
import threading
import os
import types
import itertools
import concurrent.futures as cf

from flux.job import JobspecV1, EventLogEvent, JobException
from flux.job.executor import (
    FluxExecutor,
    FluxExecutorFuture,
    _FluxExecutorThread,
)


def __flux_size():
    return 1


class ShamJobEventWatchFuture:
    """A wrapper around a LogEvent to act like a JobEventWatchFuture."""

    def __init__(self, log_event):
        self.log_event = log_event

    def get_event(self):
        return self.log_event


class TestFluxExecutor(unittest.TestCase):
    """Tests for FluxExecutor."""

    def test_as_completed(self):
        with FluxExecutor() as executor:
            jobspec = JobspecV1.from_command(["true"])
            futures = [executor.submit(jobspec) for _ in range(3)]
            for fut in cf.as_completed(futures):
                self.assertEqual(fut.result(timeout=0), 0)
                self.assertIsNone(fut.exception())

    def test_failed_submit(self):
        with FluxExecutor(thread_name_prefix="foobar") as executor:
            jobspec = JobspecV1.from_command(["false"])
            future = executor.submit(jobspec).add_jobid_callback(
                lambda future: event.set()
            )
            event = threading.Event()
            jobid = future.jobid()
            self.assertGreater(jobid, 0)
            self.assertTrue(event.is_set())
            self.assertEqual(future.result(), 1)
            self.assertIsNone(future.exception())

    def test_cancel(self):
        with FluxExecutor() as executor:
            jobspec = JobspecV1.from_command(["false"])
            for _ in range(3):
                future = executor.submit(jobspec)
                if future.cancel():
                    self.assertFalse(future.running())
                    self.assertTrue(future.cancelled())
                    with self.assertRaises(cf.CancelledError):
                        future.jobid()
                    with self.assertRaises(cf.CancelledError):
                        future.exception()
                else:
                    self.assertEqual(future.result(), 1)
                    self.assertIsNone(future.exception())

    def test_bad_jobspec(self):
        with FluxExecutor() as executor:
            future = executor.submit(None)  # not a valid jobspec
        with self.assertRaisesRegex(RuntimeError, r"job could not be submitted.*"):
            # trying to fetch jobid should raise an error
            future.jobid()
        with self.assertRaises(OSError):
            # future should be fulfilled after shutdown
            future.result(timeout=0)
        self.assertIsInstance(future.exception(), OSError)

    def test_submit_after_shutdown(self):
        executor = FluxExecutor()
        executor.shutdown(wait=True)
        with self.assertRaises(RuntimeError):
            executor.submit(JobspecV1.from_command(["true"]))
        with self.assertRaises(RuntimeError):
            executor.submit(None)

    def test_wait(self):
        with FluxExecutor(threads=3) as executor:
            jobspec = JobspecV1.from_command(["false"])
            futures = [executor.submit(jobspec) for _ in range(3)]
            done, not_done = cf.wait(futures, return_when=cf.FIRST_COMPLETED)
            self._check_done(done)
            done, not_done = cf.wait(futures, return_when=cf.FIRST_EXCEPTION)
            self._check_done(done)
            done, not_done = cf.wait(futures)
            self._check_done(done)
            self.assertEqual(len(not_done), 0)

    def _check_done(self, done_futures):
        self.assertGreater(len(done_futures), 0)
        for fut in done_futures:
            self.assertEqual(fut.result(timeout=0), 1)

    def test_executor_event_callbacks(self):
        with FluxExecutor() as executor:
            expected_events = set(["start", "finish", "depend", "priority", "free"])
            future = executor.submit(JobspecV1.from_command(["false"]))
            for event in executor.EVENTS:
                future.add_event_callback(
                    event, lambda fut, event: expected_events.discard(event.name)
                )
        self.assertFalse(expected_events)  # no more expected events

    def test_exception_event(self):
        with FluxExecutor() as executor:
            flag = threading.Event()
            future = executor.submit(JobspecV1.from_command(["/not/a/real/app"]))
            future.add_event_callback("exception", lambda fut, event: flag.set())
            self.assertIsInstance(future.exception(), JobException)
            self.assertTrue(flag.is_set())

    def test_broken_executor(self):
        with FluxExecutor() as executor:
            executor._broken_event.set()
            with self.assertRaisesRegex(RuntimeError, "Executor is broken.*"):
                executor.submit(JobspecV1.from_command(["/not/a/real/app"]))


class TestFluxExecutorThread(unittest.TestCase):
    """Simple synchronous tests for _FluxExecutorThread."""

    def test_exit_condition(self):
        deq = collections.deque()
        event = threading.Event()
        thread = _FluxExecutorThread(threading.Event(), event, deq, 0.01, (), {})
        self.assertTrue(thread._FluxExecutorThread__work_remains())
        event.set()
        self.assertFalse(thread._FluxExecutorThread__work_remains())
        deq.append(None)
        self.assertTrue(thread._FluxExecutorThread__work_remains())

    def test_bad_jobspecs(self):
        deq = collections.deque()
        event = threading.Event()
        thread = _FluxExecutorThread(threading.Event(), event, deq, 0.01, (), {})
        futures = [FluxExecutorFuture(threading.get_ident()) for _ in range(5)]
        deq.extend(((None,), {}, f) for f in futures)  # send jobspec of None
        event.set()
        thread.run()
        self.assertFalse(deq)
        self.assertFalse(thread._FluxExecutorThread__running_user_futures)
        for fut in futures:
            self.assertIsInstance(fut.exception(), OSError)

    def test_bad_submit_arguments(self):
        """send bad arguments to ``flux.job.submit``"""
        deq = collections.deque()
        event = threading.Event()
        thread = _FluxExecutorThread(threading.Event(), event, deq, 0.01, (), {})
        futures = [FluxExecutorFuture(threading.get_ident()) for _ in range(5)]
        jobspec = JobspecV1.from_command(["false"])
        deq.extend(((jobspec,), {"not_an_arg": 42}, f) for f in futures)
        event.set()
        thread.run()
        self.assertFalse(deq)
        self.assertFalse(thread._FluxExecutorThread__running_user_futures)
        for fut in futures:
            self.assertIsInstance(fut.exception(), TypeError)

    def test_cancel(self):
        deq = collections.deque()
        event = threading.Event()
        jobspec = JobspecV1.from_command(["false"])
        thread = _FluxExecutorThread(threading.Event(), event, deq, 0.01, (), {})
        futures = [FluxExecutorFuture(threading.get_ident()) for _ in range(5)]
        for fut in futures:
            deq.append(((jobspec,), {}, fut))
            fut.cancel()
        event.set()
        thread.run()
        for fut in futures:
            with self.assertRaises(cf.CancelledError):
                fut.result()
            with self.assertRaises(cf.CancelledError):
                fut.jobid()

    def test_exception_completion(self):
        jobspec = JobspecV1.from_command(["false"])
        thread = _FluxExecutorThread(
            threading.Event(), threading.Event(), collections.deque(), 0.01, (), {}
        )
        fut = FluxExecutorFuture(threading.get_ident())
        self.assertFalse(fut.done())
        fut._set_event(EventLogEvent({"name": "start", "timestamp": 0}))
        self.assertFalse(fut.done())
        thread._FluxExecutorThread__event_update(
            ShamJobEventWatchFuture(
                EventLogEvent(
                    {
                        "name": "exception",
                        "timestamp": 0,
                        "context": {"severity": 1, "type": "foobar"},
                    }
                )
            ),
            fut,
        )
        self.assertFalse(fut.done())
        thread._FluxExecutorThread__event_update(
            ShamJobEventWatchFuture(
                EventLogEvent(
                    {
                        "name": "exception",
                        "timestamp": 0,
                        "context": {"severity": 0, "type": "foobar"},
                    }
                )
            ),
            fut,
        )
        self.assertTrue(fut.done())
        self.assertIsInstance(fut.exception(), JobException)

    def test_finish_completion(self):
        thread = _FluxExecutorThread(
            threading.Event(), threading.Event(), collections.deque(), 0.01, (), {}
        )
        for exit_status in (0, 1, 15, 120, 255):
            flag = threading.Event()
            fut = FluxExecutorFuture(threading.get_ident()).add_done_callback(
                lambda fut: flag.set()
            )
            thread._FluxExecutorThread__event_update(
                ShamJobEventWatchFuture(
                    EventLogEvent(
                        {
                            "name": "finish",
                            "timestamp": 0,
                            "context": {"status": exit_status},
                        }
                    )
                ),
                fut,
            )
            self.assertTrue(fut.done())
            self.assertTrue(flag.is_set())
            if os.WIFEXITED(exit_status):
                self.assertEqual(fut.result(), os.WEXITSTATUS(exit_status))
            elif os.WIFSIGNALED(exit_status):
                self.assertEqual(fut.result(), -os.WTERMSIG(exit_status))

    def test_exception_catching(self):
        """Test that the thread exits cleanly when __run() raises."""

        def new_run(*args):
            raise TypeError("foobar")

        error_event = threading.Event()
        stop_event = threading.Event()
        stop_event.set()
        futures = [FluxExecutorFuture(threading.get_ident()) for _ in range(5)]
        running_futures = set(
            [FluxExecutorFuture(threading.get_ident()) for _ in range(5)]
        )
        deque = collections.deque([((None,), {}, fut) for fut in futures])
        thread = _FluxExecutorThread(error_event, stop_event, deque, 0.01, (), {})
        thread._FluxExecutorThread__running_user_futures.update(running_futures)
        # replace the __run method
        thread._FluxExecutorThread__run = types.MethodType(new_run, thread)
        with self.assertRaisesRegex(TypeError, "foobar"):
            thread.run()
        self.assertTrue(error_event.is_set())
        for fut in list(futures) + list(running_futures):
            with self.assertRaisesRegex(TypeError, "foobar"):
                fut.result()

    def test_exception_catching_reactor_run(self):
        """Test that the thread exits cleanly when reactor_run returns < 0"""

        def new_reactor_run(*args, **kwargs):
            # returning < 0 causes the thread to raise
            return -1

        jobspec = JobspecV1.from_command(["true"])
        error_event = threading.Event()
        stop_event = threading.Event()
        stop_event.set()
        futures = [FluxExecutorFuture(threading.get_ident()) for _ in range(5)]
        deque = collections.deque([((jobspec,), {}, fut) for fut in futures])
        thread = _FluxExecutorThread(error_event, stop_event, deque, 0.01, (), {})
        # replace the reactor_run method of the thread's flux handle
        thread._FluxExecutorThread__flux_handle.reactor_run = types.MethodType(
            new_reactor_run, thread._FluxExecutorThread__flux_handle
        )
        with self.assertRaisesRegex(RuntimeError, "reactor start failed"):
            thread.run()
        self.assertTrue(error_event.is_set())
        for fut in list(futures):
            with self.assertRaisesRegex(RuntimeError, "reactor start failed"):
                fut.result()


class TestFluxExecutorFuture(unittest.TestCase):
    """Tests for FluxExecutorFuture."""

    def test_set_jobid(self):
        for jobid in (21, 594240):
            fut = FluxExecutorFuture(threading.get_ident())
            with self.assertRaises(cf.TimeoutError):
                fut.jobid(timeout=0)
            fut._set_jobid(jobid)
            self.assertEqual(jobid, fut.jobid(timeout=0))
            with self.assertRaises(RuntimeError):
                fut._set_jobid(jobid)

    def test_jobid_callback(self):
        fut = FluxExecutorFuture(threading.get_ident())
        flag = threading.Event()
        fut.add_jobid_callback(lambda f: flag.set())
        self.assertFalse(flag.is_set())
        fut._set_jobid(5)
        self.assertTrue(flag.is_set())
        # now check that adding callbacks fires them immediately
        flag.clear()
        fut.add_jobid_callback(lambda f: flag.set())
        self.assertTrue(flag.is_set())

    def test_event_callback(self):
        fut = FluxExecutorFuture(threading.get_ident())
        flag = threading.Event()
        for state in ("clean", "start", "alloc"):
            flag.clear()
            fut.add_event_callback(
                state, lambda f, log: (flag.set(), self.assertEqual(log.name, state))
            )
            log_event = EventLogEvent({"name": state, "timestamp": 0})
            fut._set_event(log_event)
            self.assertTrue(flag.is_set())
            # now check that adding callbacks fires them immediately
            flag.clear()
            fut.add_event_callback(
                state, lambda f, log: (flag.set(), self.assertEqual(log.name, state))
            )
            self.assertTrue(flag.is_set())

    def test_bad_event(self):
        fut = FluxExecutorFuture(threading.get_ident())
        event = "not an event"
        log_event = EventLogEvent({"name": event, "timestamp": 0})
        with self.assertRaises(ValueError):
            fut._set_event(log_event)
        with self.assertRaises(ValueError):
            fut.add_event_callback(event, None)

    def test_callback_deadlock(self):
        flag = threading.Event()
        # try waiting for the future from the callback
        fut = FluxExecutorFuture(threading.get_ident()).add_jobid_callback(
            lambda fut: (fut.result(), flag.set())
        )
        fut._set_jobid(5)
        # check that flag wasn't set---because the callback raised
        self.assertFalse(flag.is_set())
        # try the same with an event callback
        log_event = EventLogEvent({"name": "debug", "timestamp": 0})
        fut.add_event_callback(
            log_event.name, lambda fut, event: (fut.result(), flag.set())
        )
        fut._set_event(log_event)
        self.assertFalse(flag.is_set())
        # now complete the future and try again
        fut.set_result(21)
        fut.add_jobid_callback(lambda fut: (fut.result(), flag.set()))
        self.assertTrue(flag.is_set())
        flag.clear()
        fut.add_event_callback(
            log_event.name, lambda fut, event: (fut.result(), flag.set())
        )
        self.assertTrue(flag.is_set())

    def test_multiple_events(self):
        fut = FluxExecutorFuture(threading.get_ident())
        counter = itertools.count()
        fut.add_event_callback("urgency", lambda fut, event: next(counter))
        total_iterations = 5
        for _ in range(5):
            fut._set_event(EventLogEvent({"name": "urgency", "timestamp": 0}))
        self.assertEqual(next(counter), total_iterations)
        new_counter = itertools.count()
        fut.add_event_callback("urgency", lambda fut, event: next(new_counter))
        self.assertEqual(next(new_counter), total_iterations)
        fut._set_event(EventLogEvent({"name": "urgency", "timestamp": 0}))
        # invoking the callback increments the counter, as does checking the counter
        self.assertEqual(next(counter), total_iterations + 2)
        self.assertEqual(next(new_counter), total_iterations + 2)

    def test_adding_callbacks_from_callbacks(self):
        fut = FluxExecutorFuture(threading.get_ident())
        flag = threading.Event()
        nested_callback = lambda fut, event: flag.set()
        fut.add_event_callback(
            "start", lambda fut, event: fut.add_event_callback("start", nested_callback)
        )
        fut._set_event(EventLogEvent({"name": "start", "timestamp": 0}))
        self.assertTrue(flag.is_set())

    def test_multiple_cancel(self):
        fut = FluxExecutorFuture(threading.get_ident())
        invocations = itertools.count()
        fut.add_done_callback(lambda _: next(invocations))
        self.assertFalse(fut.cancelled())
        for _ in range(3):  # test cancelling more than once
            self.assertTrue(fut.cancel())
        self.assertEqual(next(invocations), 1)
        with self.assertRaises(cf.CancelledError):
            fut.jobid()


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())

# vi: ts=4 sw=4 expandtab
