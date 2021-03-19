###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""
This module defines the ``FluxExecutor`` and ``FluxExecutorFuture`` classes.
"""

import threading
import logging
import itertools
import collections
import concurrent.futures
import weakref
import time
import os

import flux
from flux.job.submit import submit_async, submit_get_id
from flux.job.event import event_watch_async, JobException, MAIN_EVENTS


class _FluxExecutorThread(threading.Thread):
    """Thread that submits jobs to Flux and waits for event updates.

    Completes FluxExecutorFutures as events indicate that they finish.

    :param exit_event: ``threading.Event`` indicating when the associated
        Executor has shut down.
    :param jobspecs_to_submit: a queue filled with jobspecs by the Executor
    :param poll_interval: the interval (in seconds) to check for new jobs.
    """

    # pylint: disable=too-many-arguments
    def __init__(
        self,
        exit_event,
        jobspecs_to_submit,
        poll_interval,
        handle_args,
        handle_kwargs,
        **kwargs,
    ):
        super().__init__(**kwargs)
        self.__exit_event = exit_event
        self.__jobspecs_to_submit = jobspecs_to_submit
        self.__remaining_flux_futures = 0  # number of unfulfilled futures
        self.__poll_interval = poll_interval
        self.__flux_handle = flux.Flux(*handle_args, **handle_kwargs)

    def run(self):
        """Loop indefinitely, submitting jobspecs and fetching jobids."""
        self.__flux_handle.timer_watcher_create(
            self.__poll_interval, self.__submit_new_jobs, repeat=self.__poll_interval
        ).start()
        while self.__work_remains():
            self.__submit_new_jobs(reactor_run=False)
            if self.__flux_handle.reactor_run() < 0:
                msg = "reactor start failed"
                self.__flux_handle.fatal_error(msg)
                raise RuntimeError(msg)

    def __work_remains(self):
        """Return True if and only if there is still work to be done.

        Equivalently, return False if it is safe to exit.
        """
        return (
            not self.__exit_event.is_set()
            or self.__jobspecs_to_submit
            or self.__remaining_flux_futures > 0
        )

    def __submit_new_jobs(self, *_, reactor_run=True):
        """Pull jobspecs from the queue and submit them.

        Invoked on a timer, and passed several arguments, none
        of which are used---hence the "*_"
        """
        if not self.__work_remains() and reactor_run:
            self.__flux_handle.reactor_stop()
        if self.__remaining_flux_futures == 0 and not self.__jobspecs_to_submit:
            time.sleep(self.__poll_interval)
        while self.__jobspecs_to_submit:
            try:
                args, kwargs, user_future = self.__jobspecs_to_submit.popleft()
            except IndexError:
                continue
            if user_future.set_running_or_notify_cancel():
                try:
                    submit_async(self.__flux_handle, *args, **kwargs).then(
                        self.__submission_callback, user_future
                    )
                except Exception as submit_exc:  # pylint: disable=broad-except
                    user_future.set_exception(submit_exc)
                else:
                    self.__remaining_flux_futures += 1

    def __submission_callback(self, submission_future, user_future):
        """Callback invoked when a jobid is ready for a submitted jobspec."""
        jobid = submit_get_id(submission_future)
        user_future._set_jobid(jobid)  # pylint: disable=protected-access
        event_watch_async(self.__flux_handle, jobid).then(
            self.__event_update, user_future
        )

    def __event_update(self, event_future, user_future):
        """Callback invoked when a job has an event update."""
        event = event_future.get_event()
        if event is not None:
            if event.name in user_future.EVENTS:
                user_future._set_event(event)  # pylint: disable=protected-access
            # check if the event tells us that the job is done
            if not user_future.done():
                if event.name == "finish":
                    exit_status = event.context["status"]
                    if os.WIFEXITED(exit_status):
                        user_future.set_result(os.WEXITSTATUS(exit_status))
                    elif os.WIFSIGNALED(exit_status):
                        user_future.set_result(-os.WTERMSIG(exit_status))
                    else:
                        user_future.set_exception(ValueError(exit_status))
                elif event.name == "exception" and event.context["severity"] == 0:
                    user_future.set_exception(JobException(event))
        else:  # no more events
            self.__remaining_flux_futures -= 1


class FluxExecutorFuture(concurrent.futures.Future):
    """A ``concurrent.futures.Future`` subclass that represents a single Flux job.

    In addition to all of the ``concurrent.futures.Future`` functionality,
    ``FluxExecutorFuture`` instances offer:

    * The ``jobid`` and ``add_jobid_callback`` methods for retrieving the
      Flux jobid of the underlying job.
    * The ``add_event_callback`` method to invoke callbacks when particular
      job-state events occur.

    Valid events are contained in the ``EVENTS`` class attribute.
    """

    #: A set containing the names of valid events.
    EVENTS = MAIN_EVENTS

    def __init__(self, owning_thread_id, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Thread.ident of thread tasked with completing this future
        self.__owning_thread_id = owning_thread_id
        self.__jobid_condition = threading.Condition()
        self.__jobid = None
        self.__jobid_callbacks = []
        self.__event_lock = threading.RLock()
        self.__events_occurred = {state: collections.deque() for state in self.EVENTS}
        self.__event_callbacks = {state: collections.deque() for state in self.EVENTS}

    def _set_jobid(self, jobid):
        """Sets the Flux jobid associated with the future.

        Should only be used by Executor implementations and unit tests.
        """
        if self.__jobid is not None:
            raise RuntimeError("invalid state")  # should be InvalidStateError in 3.8+
        with self.__jobid_condition:
            self.__jobid = jobid
            self.__jobid_condition.notify_all()
        for callback in self.__jobid_callbacks:
            self._invoke_flux_callback(callback)

    def add_done_callback(self, *args, **kwargs):  # pylint: disable=arguments-differ
        """Attaches a callable that will be called when the future finishes.

        :param fn: A callable that will be called with this future as its only
            argument when the future completes or is cancelled. The callable
            will always be called by a thread in the same process in which
            it was added. If the future has already completed or been
            cancelled then the callable will be called immediately. These
            callables are called in the order that they were added.
        :return: ``self``
        """
        super().add_done_callback(*args, **kwargs)
        return self

    def jobid(self, timeout=None):
        """Return the jobid of the Flux job that the future represents.

        :param timeout: The number of seconds to wait for the jobid.
            If None, then there is no limit on the wait time.

        :return: a positive integer jobid, or ``-1`` if the future was cancelled.

        :raises concurrent.futures.TimeoutError: If the jobid is not available
            before the given timeout.
        """
        if self.__jobid is not None:
            return self.__jobid
        with self.__jobid_condition:
            self.__jobid_condition.wait(timeout)
            if self.__jobid is not None:
                return self.__jobid
            raise concurrent.futures.TimeoutError()

    def add_jobid_callback(self, callback):
        """Attaches a callable that will be called when the jobid is ready.

        Added callables are called in the order that they were added and may be called
        in another thread.  If the callable raises an ``Exception`` subclass, it will
        be logged and ignored.  If the callable raises a ``BaseException`` subclass,
        the behavior is undefined.

        :param callback: a callable taking the future as its only argument.
        :return: ``self``
        """
        with self.__jobid_condition:
            if self.__jobid is None:
                self.__jobid_callbacks.append(callback)
                return self
        self._invoke_flux_callback(callback)
        return self

    def _invoke_flux_callback(self, callback, *args):
        try:
            callback(self, *args)
        except Exception:  # pylint: disable=broad-except
            logging.getLogger(__name__).exception(
                "exception calling callback for %r", self
            )

    def exception(self, *args, **kwargs):  # pylint: disable=arguments-differ
        """If this method is invoked from a jobid callback by an executor thread,
        it will result in deadlock, since the current thread will wait
        for work that the same thread is meant to do.

        Head off this possibility by checking the current thread.
        """
        if not self.done() and threading.get_ident() == self.__owning_thread_id:
            raise RuntimeError("Cannot wait for future from inside callback")
        return super().exception(*args, **kwargs)

    exception.__doc__ = concurrent.futures.Future.exception.__doc__

    def result(self, *args, **kwargs):  # pylint: disable=arguments-differ
        """If this method is invoked from a jobid callback by an executor thread,
        it will result in deadlock, since the current thread will wait
        for work that the same thread is meant to do.

        Head off this possibility by checking the current thread.
        """
        if not self.done() and threading.get_ident() == self.__owning_thread_id:
            raise RuntimeError("Cannot wait for future from inside callback")
        return super().result(*args, **kwargs)

    result.__doc__ = concurrent.futures.Future.result.__doc__

    def cancel(self, *args, **kwargs):  # pylint: disable=arguments-differ
        """If a thread is waiting for the future's jobid, and another
        thread cancels the future, the waiting thread would never wake up
        because the jobid would never be set.

        When cancelling, set the jobid to something invalid.
        """
        cancelled = super().cancel(*args, **kwargs)
        if cancelled:
            self._set_jobid(-1)
        return cancelled

    cancel.__doc__ = concurrent.futures.Future.cancel.__doc__

    def add_event_callback(self, event, callback):
        """Add a callback to be invoked when an event occurs.

        The callback will be invoked, with the future as the first argument and the
        ``flux.job.EventLogEvent`` as the second, whenever the event occurs. If the
        event occurs multiple times, the callback will be invoked with each different
        `EventLogEvent` instance. If the event never occurs, the callback
        will never be invoked.

        Added callables are called in the order that they were added and may be called
        in another thread.  If the callable raises an ``Exception`` subclass, it will
        be logged and ignored.  If the callable raises a ``BaseException`` subclass,
        the behavior is undefined.

        If the event has already occurred, the callback will be called immediately.

        :param event: the name of the event to add the callback to.
        :param callback: a callable taking the future and the event as arguments.
        :return: ``self``
        """
        if event not in self.EVENTS:
            raise ValueError(event)
        with self.__event_lock:
            self.__event_callbacks[event].append(callback)
            for log_entry in self.__events_occurred[event]:
                self._invoke_flux_callback(callback, log_entry)
        return self

    def _set_event(self, log_entry):
        """Set an event on the future.

        For use by Executor implementations and unit tests.

        :param log_entry: an ``EventLogEvent``.
        """
        event_name = log_entry.name
        if event_name not in self.EVENTS:
            raise ValueError(event_name)
        with self.__event_lock:
            self.__events_occurred[event_name].append(log_entry)
            # make a shallow copy of callbacks --- in case a user callback
            # tries to add another callback for the same event
            for callback in list(self.__event_callbacks[event_name]):
                self._invoke_flux_callback(callback, log_entry)


class FluxExecutor:
    """Provides a method to submit jobs to Flux asynchronously.

    Forks threads to complete futures and fetch event updates in the background.

    Inspired by the ``concurrent.futures.Executor`` class, with the following
    interface differences:

        - the ``submit`` method takes a ``flux.job.Jobspec`` instead of a
          callable and its arguments, and returns a ``FluxExecutorFuture``
          representing that job.
        - the ``map`` method is not supported, given that the executor consumes
          Jobspecs rather than callables.

    Otherwise, the FluxExecutor is faithful to its inspiration. In addition
    to methods and behavior defined by ``concurrent.futures``, FluxExecutor
    provides its futures with event updates and the jobid of the underlying job.

    Returned futures have their jobid set as soon as it is available, which is
    always before the future completes.

    Futures may receive event updates even after they complete. The names
    of valid events are contained in the ``EVENTS`` class attribute.

    The result of a future is the highest process exit status of the underlying job
    (in which case the result is an integer greater than or equal to 0),
    or ``-signum`` where ``signum`` is the number
    of the signal that caused the process to terminate
    (in which case the result is an integer less than 0).

    A future is marked as "running" (and can no longer be canceled using the
    ``.cancel()`` method) once the associated jobspec
    has been submitted to Flux. The underlying Flux job may still be
    canceled, however, using the ``flux.job.cancel`` and
    ``flux.job.kill`` functions, in which case a ``JobException`` will be set.

    If the jobspec is invalid, an ``OSError`` is set.

    :param threads: the number of worker threads to fork.
    :param thread_name_prefix: used to control the names of ``threading.Thread``
        objects created by the executor, for easier debugging.
    :param poll_interval: the interval (in seconds) in which to break out of the
        flux event loop to check for new job submissions.
    :param handle_args: positional arguments to the ``flux.Flux`` instances used by
        the executor.
    :param handle_kwargs: keyword arguments to the ``flux.Flux`` instances used by
        the executor.
    """

    # Used to assign unique thread names when thread_name_prefix is not supplied.
    _counter = itertools.count().__next__
    #: A set containing valid event names for attaching to futures.
    EVENTS = MAIN_EVENTS

    # pylint: disable=too-many-arguments,dangerous-default-value
    def __init__(
        self,
        threads=1,
        thread_name_prefix="",
        poll_interval=0.1,
        handle_args=(),
        handle_kwargs={},
    ):
        if threads < 0:
            raise ValueError("the number of threads must be > 0")
        # split jobs equally among threads; give them each their own queue
        self._submission_queues = [collections.deque() for i in range(threads)]
        self._next_thread = 0  # the next thread to give a job to
        self._shutdown_lock = threading.Lock()
        self._shutdown_event = threading.Event()
        thread_name_prefix = (
            thread_name_prefix or f"{type(self).__name__}-{self._counter()}"
        )
        self._executor_threads = [
            _FluxExecutorThread(
                self._shutdown_event,
                deque,
                poll_interval,
                handle_args,
                handle_kwargs,
                name=(f"{thread_name_prefix}-{i}"),
                daemon=True,
            )
            for i, deque in enumerate(self._submission_queues)
        ]
        for thread in self._executor_threads:
            thread.start()
        # register a finalizer to ensure worker threads are notified to shut down
        self._finalizer = weakref.finalize(
            self, self._shutdown_threads, self._shutdown_event, self._executor_threads
        )

    def shutdown(self, wait=True, *, cancel_futures=False):
        """Clean-up the resources associated with the Executor.

        It is safe to call this method several times. Otherwise, no other
        methods can be called after this one.

        :param wait: If ``True``, then this method will not return until all running
            futures have finished executing and the resources used by the
            executor have been reclaimed.
        :param cancel_futures: If ``True``, this method will cancel all pending
            futures that the executor has not started running. Any futures that
            are completed or running won't be cancelled, regardless of the value
            of ``cancel_futures``.
        """
        with self._shutdown_lock:
            self._shutdown_event.set()
        if cancel_futures:
            # Drain all work items from the queues, and then cancel their
            # associated futures.
            for deque in self._submission_queues:
                while deque:
                    try:
                        _, _, user_future = deque.popleft()
                    except IndexError:
                        pass
                    else:
                        user_future.cancel()
                        user_future.set_running_or_notify_cancelled()
        if wait:
            for thread in self._executor_threads:
                thread.join()

    def submit(self, *args, **kwargs):
        """Submit a jobspec to Flux and return a ``FluxExecutorFuture``.

        Accepts the same positional and keyword arguments as ``flux.job.submit``,
        except for the ``flux.job.submit`` function's first argument, ``flux_handle``.

        :param jobspec: jobspec defining the job request
        :type jobspec: Jobspec or its string encoding
        :param urgency: job urgency 0 (lowest) through 31 (highest)
            (default is 16).  Priorities 0 through 15 are restricted to
            the instance owner.
        :type urgency: int
        :param waitable: allow result to be fetched with ``flux.job.wait()``
            (default is False).  Waitable=True is restricted to the
            instance owner.
        :type waitable: bool
        :param debug: enable job manager debugging events to job eventlog
            (default is False)
        :type debug: bool
        :param pre_signed: jobspec argument is already signed
            (default is False)
        :type pre_signed: bool

        :raises RuntimeError: if ``shutdown`` has been called.
        """
        with self._shutdown_lock:
            if self._shutdown_event.is_set():
                raise RuntimeError("cannot schedule new futures after shutdown")
            future_owner_id = self._executor_threads[self._next_thread].ident
            fut = FluxExecutorFuture(future_owner_id)
            self._submission_queues[self._next_thread].append((args, kwargs, fut))
            self._next_thread = (self._next_thread + 1) % len(self._submission_queues)
            return fut

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.shutdown(wait=True)
        return False

    @staticmethod
    def _shutdown_threads(event, threads):
        """Set the threading.Event and join all threads.

        Not a method so as not to prevent garbage collection
        (see `weakref.finalize` docs).
        """
        event.set()
        for thread in threads:
            thread.join()
