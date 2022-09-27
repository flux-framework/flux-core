###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import selectors
import signal

import flux.constants
from flux.core.watchers import FDWatcher


def stop_callback(handle, _watcher, _fd_int, events, args=None):
    """
    This is called by the watcher, and stops the reactor add adds ready jobs.
    """
    handle.reactor_stop()

    # TimerWatcher won't know a file descriptor, just needs to stop the reactor
    if not args:
        return
    selector = args["select"]
    selector_event = get_selector_event(events)
    selector.ready.add((args["key"], selector_event & args["key"].events))


def get_selector_event(events):
    """
    Given an int, return the corresponding flux identifier.
    """
    event = 0
    if events & flux.constants.FLUX_POLLIN:
        event |= selectors.EVENT_READ
    if events & flux.constants.FLUX_POLLOUT:
        event |= selectors.EVENT_WRITE
    return event


def get_flux_event(events):
    """
    Given an int, return the corresponding selector identifier
    """
    event = 0
    if events & selectors.EVENT_READ:
        event |= flux.constants.FLUX_POLLIN
    if events & selectors.EVENT_WRITE:
        event |= flux.constants.FLUX_POLLOUT
    return event


class FluxSelector(
    selectors._BaseSelectorImpl
):  # pylint: disable=protected-access # type: ignore
    """
    A Flux selector supports registering file objects to be monitored for
    specific I/O events (for Flux).
    """

    def __init__(self, handle):
        super().__init__()
        self.handle = handle
        self.ready = set()
        self._watchers = {}

    def register(self, fileobj, events, data=None):
        """
        Register a new file descriptor event.
        """
        key = super().register(fileobj, events, data)
        watcher = FDWatcher(
            self.handle,
            fileobj,
            get_flux_event(events),
            stop_callback,
            args={"key": key, "select": self},
        )
        watcher.start()
        self._watchers[fileobj] = watcher
        return key

    def unregister(self, fileobj):
        """
        Remove the key and the watcher.
        """
        try:
            key = self._fd_to_key.pop(self._fileobj_lookup(fileobj))
            self._watchers[key.fileobj].stop()
        except KeyError:
            raise KeyError("{!r} is not registered".format(fileobj)) from None
        return key

    def select(self, timeout=None):
        """
        Perform the actual selection, until some monitored file objects are
        ready or a timeout expires.
        Parameters:
        timeout -- if timeout > 0, this specifies the maximum wait time, in
                   seconds, waited for by a flux TimerWatcher
                   if timeout <= 0, the select() call won't block, and will
                   report the currently ready file objects
                   if timeout is None, select() will block until a monitored
                   file object becomes ready
        Returns:
        list of (key, events) for ready file objects
        `events` is a bitwise mask of EVENT_READ|EVENT_WRITE
        """
        reactor = self.handle.get_reactor()
        reactor_interrupted = False

        def reactor_interrupt(handle, *_args):
            #  ensure reactor_interrupted from enclosing scope:
            nonlocal reactor_interrupted
            reactor_interrupted = True
            handle.reactor_stop(reactor)

        with self.handle.signal_watcher_create(signal.SIGINT, reactor_interrupt):
            with self.handle.in_reactor():

                # Ensure previous events are cleared
                self.ready.clear()

                # 0 == "run until I tell you to stop"
                if timeout is not None:
                    if timeout > 0:

                        # Block for a specified timeout
                        with self.handle.timer_watcher_create(timeout, stop_callback):
                            watcher_count = self.handle.flux_reactor_run(reactor, 0)

                    # If timeout <= 0, select won't block
                    else:
                        watcher_count = self.handle.flux_reactor_run(
                            reactor, flux.constants.FLUX_REACTOR_NOWAIT
                        )

                # If timeout is None, block until a monitored object ready
                else:
                    watcher_count = self.handle.flux_reactor_run(reactor, 0)

            if reactor_interrupted:
                raise KeyboardInterrupt

            if watcher_count < 0:
                self.handle.raise_if_exception()

        return list(self.ready)
