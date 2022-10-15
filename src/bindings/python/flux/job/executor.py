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

import collections
import concurrent.futures
import itertools
import logging
import os
import threading
import time
import weakref

import flux
from flux.job.event import MAIN_EVENTS, JobException, event_watch_async
from flux.job.submit import submit_async, submit_get_id

_SubmitPackage = collections.namedtuple(
    "_SubmitPackage", ["submit_args", "submit_kwargs", "future"]
)


class _AttachPackage:  # pylint: disable=too-few-public-methods
    """Namedtuple-esque class. Constructor sets jobid on future."""

    __slots__ = ("jobid", "future")

    def __init__(self, jobid, fut):
        self.jobid = jobid
        self.future = fut
        self.future._set_jobid(jobid)  # pylint: disable=protected-access


class _FluxExecutorThread(threading.Thread):
    """Thread that submits jobs to Flux and waits for event updates.

    Completes FluxExecutorFutures as events indicate that they finish.

    :param exit_event: ``threading.Event`` indicating when the associated
        Executor has shut down.
    :param packages_to_handle: a queue filled with Packages by the Executor
    :param poll_interval: the interval (in seconds) to check for new jobs.
    """

    # pylint: disable=too-many-arguments
    def __init__(
        self,
        broken_event,
        exit_event,
        packages_to_handle,
        poll_interval,
        handle_args,
        handle_kwargs,
        **kwargs,
    ):
        super().__init__(**kwargs)
        self.__broken_event = broken_event
        self.__exit_event = exit_event
        self.__packages_to_handle = packages_to_handle
        self.__poll_interval = poll_interval
        self.__flux_handle = flux.Flux(*handle_args, **handle_kwargs)
        self.__running_user_futures = set()  # unfulfilled futures

    def run(self):
        try:
            self.__run()
        except Exception as exc:
            self.__broken_event.set()
            for fut in self.__running_user_futures:
                if not fut.done():
                    fut.set_exception(exc)
            while self.__packages_to_handle or not self.__exit_event.is_set():
                try:
                    package = self.__packages_to_handle.popleft()
                except IndexError:
                    time.sleep(0.01)
                else:
                    package.future.set_exception(exc)
            raise

    def __run(self):
        """Loop indefinitely, submitting jobspecs and fetching jobids."""
        self.__flux_handle.timer_watcher_create(
            self.__poll_interval, self.__submit_new_jobs, repeat=self.__poll_interval
        ).start()
        while self.__work_remains():
            self.__submit_new_jobs(reactor_run=False)
            if self.__flux_handle.reactor_run() < 0:
                raise RuntimeError("reactor start failed")

    def __work_remains(self):
        """Return True if and only if there is still work to be done.

        Equivalently, return False if it is safe to exit.
        """
        return (
            not self.__exit_event.is_set()
            or self.__packages_to_handle
            or self.__running_user_futures
        )

    def __submit_new_jobs(self, *_, reactor_run=True):
        """Pull jobspecs from the queue and submit them.

        Invoked on a timer, and passed several arguments, none
        of which are used---hence the "*_"
        """
        if not self.__work_remains() and reactor_run:
            self.__flux_handle.reactor_stop()
        if not self.__running_user_futures and not self.__packages_to_handle:
            time.sleep(self.__poll_interval)
        while self.__packages_to_handle:
            try:
                package = self.__packages_to_handle.popleft()
            except IndexError:
                continue
            if package.future.set_running_or_notify_cancel():
                if isinstance(package, _SubmitPackage):
                    self.__handle_submit(package)
                else:
                    self.__handle_attach(package)

    def __handle_submit(self, package):
        """Submit a _SubmitPackage and set a jobid callback."""
        try:
            submit_async(
                self.__flux_handle, *package.submit_args, **package.submit_kwargs
            ).then(self.__submission_callback, package.future)
        except Exception as submit_exc:  # pylint: disable=broad-except
            package.future.set_exception(submit_exc)
        else:
            self.__running_user_futures.add(package.future)

    def __handle_attach(self, package):
        """Submit an _AttachPackage and set an event callback."""
        try:
            event_watch_async(self.__flux_handle, package.jobid).then(
                self.__event_update, package.future
            )
        except Exception as event_exc:  # pylint: disable=broad-except
            package.future.set_exception(event_exc)
        else:
            self.__running_user_futures.add(package.future)

    def __submission_callback(self, submission_future, user_future):
        """Callback invoked when a jobid is ready for a submitted jobspec."""
        jobid = submit_get_id(submission_future)
        user_future._set_jobid(jobid)  # pylint: disable=protected-access
        event_watch_async(self.__flux_handle, jobid).then(
            self.__event_update, user_future
        )

    def __event_update(self, event_future, user_future):
        """Callback invoked when a job has an event update."""
        event = None
        try:
            event = event_future.get_event()
        except FileNotFoundError:  # job ID was not accepted
            user_future.set_exception(ValueError("job ID does not match any job"))
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
            self.__running_user_futures.discard(user_future)


# pylint: disable=too-many-instance-attributes
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
        self.__jobid_set = False  # True if the jobid has been set to something
        self.__jobid_exception = None
        self.__jobid_callbacks = []
        self.__event_lock = threading.RLock()
        self.__events_occurred = {state: collections.deque() for state in self.EVENTS}
        self.__event_callbacks = {state: collections.deque() for state in self.EVENTS}

    def _set_jobid(self, jobid, exc=None):
        """Sets the Flux jobid associated with the future.

        If `exc` is not None, raise `exc` instead of returning the jobid
        in calls to `Future.jobid()`. Useful if the job ID cannot be
        retrieved.

        Should only be used by Executor implementations and unit tests.
        """
        if self.__jobid_set:
            # should be InvalidStateError in 3.8+
            raise RuntimeError("invalid state: jobid already set")
        with self.__jobid_condition:
            self.__jobid = jobid
            self.__jobid_set = True
            if exc is not None:
                self.__jobid_exception = exc
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

        :return: a positive integer jobid.

        :raises concurrent.futures.TimeoutError: If the jobid is not available
            before the given timeout.
        :raises concurrent.futures.CancelledError: If the future was cancelled.
        :raises RuntimeError: If the job could not be submitted (e.g. if
            the jobspec was invalid).
        """
        if self.__jobid_set:
            return self._get_jobid()
        with self.__jobid_condition:
            self.__jobid_condition.wait(timeout)
            if self.__jobid_set:
                return self._get_jobid()
            raise concurrent.futures.TimeoutError()

    def _get_jobid(self):
        """Get the jobid, checking for cancellation and invalid job ids."""
        if self.__jobid_exception is not None:
            raise self.__jobid_exception
        return self.__jobid

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
        """If this method is invoked from a jobid/event callback by an executor thread,
        it will result in deadlock, since the current thread will wait
        for work that the same thread is meant to do.

        Head off this possibility by checking the current thread.
        """
        if not self.done() and threading.get_ident() == self.__owning_thread_id:
            raise RuntimeError("Cannot wait for future from inside callback")
        return super().exception(*args, **kwargs)

    exception.__doc__ = concurrent.futures.Future.exception.__doc__

    def result(self, *args, **kwargs):  # pylint: disable=arguments-differ
        """If this method is invoked from a jobid/event callback by an executor thread,
        it will result in deadlock, since the current thread will wait
        for work that the same thread is meant to do.

        Head off this possibility by checking the current thread.
        """
        if not self.done() and threading.get_ident() == self.__owning_thread_id:
            raise RuntimeError("Cannot wait for future from inside callback")
        return super().result(*args, **kwargs)

    result.__doc__ = concurrent.futures.Future.result.__doc__

    def set_exception(self, exception):
        """When setting an exception on the future, set the jobid if it hasn't
        been set already. The jobid will already have been set unless the exception
        was generated before the job could be successfully submitted.
        """
        try:
            self.jobid(0)
        except concurrent.futures.TimeoutError:
            # set jobid to something
            self._set_jobid(
                None, RuntimeError(f"job could not be submitted due to {exception}")
            )
        return super().set_exception(exception)

    set_exception.__doc__ = concurrent.futures.Future.set_exception.__doc__

    def cancel(self, *args, **kwargs):  # pylint: disable=arguments-differ
        """If a thread is waiting for the future's jobid, and another
        thread cancels the future, the waiting thread would never wake up
        because the jobid would never be set.

        When cancelling, set the jobid to something invalid.
        """
        if self.cancelled():  # if already cancelled, return True
            return True
        cancelled = super().cancel(*args, **kwargs)
        if cancelled:
            try:
                self.jobid(0)
            except concurrent.futures.TimeoutError:
                # set jobid to something
                self._set_jobid(None, concurrent.futures.CancelledError())
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
    """Provides a method to submit and monitor Flux jobs asynchronously.

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

    Futures returned by ``submit`` have their jobid set as soon as it is available,
    which is always before the future completes.

    The executor can also monitor existing jobs through the ``attach`` method,
    which takes a job ID and returns a future representing the job.

    Futures may receive event updates even after they complete. The names
    of valid events are contained in the ``EVENTS`` class attribute.

    The result of a future is the highest process exit status of the underlying job
    (in which case the result is an integer greater than or equal to 0),
    or ``-signum`` where ``signum`` is the number
    of the signal that caused the process to terminate
    (in which case the result is an integer less than 0).

    A future is marked as "running" (and can no longer be canceled using the
    ``.cancel()`` method) once it reaches a certain point in the Executor---a point
    which is completely unrelated to the status of the underlying Flux job.
    The underlying Flux job may still be canceled at any point before it terminates,
    however, using the ``flux.job.cancel`` and ``flux.job.kill`` functions,
    in which case a ``JobException`` will be set.

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
        self._broken_event = threading.Event()
        self._shutdown_event = threading.Event()
        thread_name_prefix = (
            thread_name_prefix or f"{type(self).__name__}-{self._counter()}"
        )
        self._executor_threads = [
            _FluxExecutorThread(
                self._broken_event,
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
                        package = deque.popleft()
                    except IndexError:
                        pass
                    else:
                        package.future.cancel()
                        package.future.set_running_or_notify_cancelled()
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

        :raises RuntimeError: if ``shutdown`` has been called or if an error has
            occurred and new jobs cannot be submitted (e.g. a remote Flux instance
            can no longer be communicated with).
        """
        return self._create_future(_SubmitPackage, args, kwargs)

    def attach(self, jobid):
        """Attach a ``FluxExecutorFuture`` to an existing job ID and return it.

        Returned futures will behave identically to futures returned by the
        ``FluxExecutor.submit`` method. If the job ID is not accepted by Flux
        an exception will be set on the future.

        This method is primarily useful for monitoring jobs that have been
        submitted through other mechanisms.

        :param jobid: jobid to attach to.
        :type jobid: int

        :raises RuntimeError: if ``shutdown`` has been called or if an error has
            occurred and new jobs cannot be submitted (e.g. a remote Flux instance
            can no longer be communicated with).
        """
        return self._create_future(_AttachPackage, jobid)

    def _create_future(self, factory, *factory_args):
        if self._broken_event.is_set():
            raise RuntimeError("Executor is broken, new futures cannot be scheduled")
        with self._shutdown_lock:
            if self._shutdown_event.is_set():
                raise RuntimeError("cannot schedule new futures after shutdown")
            future_owner_id = self._executor_threads[self._next_thread].ident
            fut = FluxExecutorFuture(future_owner_id)
            self._submission_queues[self._next_thread].append(
                factory(*factory_args, fut)
            )
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
