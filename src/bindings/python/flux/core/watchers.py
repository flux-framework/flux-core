###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import abc
from flux.core.inner import raw, lib, ffi

__all__ = ["TimerWatcher", "FDWatcher"]


class Watcher(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self, handle=None):
        self.handle = handle

    def __enter__(self):
        """Allow this to be used as a context manager"""
        self.start()
        return self

    def __exit__(self, type_arg, value, _):
        """Allow this to be used as a context manager"""
        self.stop()
        return False

    def __del__(self):
        if self.handle is not None:
            self.destroy()

    def start(self):
        raw.flux_watcher_start(self.handle)

    def stop(self):
        raw.flux_watcher_stop(self.handle)

    def destroy(self):
        if self.handle is not None:
            raw.flux_watcher_destroy(self.handle)
            self.handle = None


@ffi.def_extern()
def timeout_handler_wrapper(unused1, unused2, revents, opaque_handle):
    del unused1, unused2  # unused arguments
    watcher = ffi.from_handle(opaque_handle)
    watcher.callback(watcher.flux_handle, watcher, revents, watcher.args)


class TimerWatcher(Watcher):
    def __init__(self, flux_handle, after, callback, repeat=0, args=None):
        self.flux_handle = flux_handle
        self.after = after
        self.repeat = repeat
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(TimerWatcher, self).__init__(
            raw.flux_timer_watcher_create(
                raw.flux_get_reactor(flux_handle),
                float(after),
                float(repeat),
                lib.timeout_handler_wrapper,
                self.wargs,
            )
        )


@ffi.def_extern()
def fd_handler_wrapper(unused1, unused2, revents, opaque_handle):
    del unused1, unused2  # unused arguments
    watcher = ffi.from_handle(opaque_handle)
    fd_int = raw.fd_watcher_get_fd(watcher.handle)
    watcher.callback(watcher.flux_handle, watcher, fd_int, revents, watcher.args)


class FDWatcher(Watcher):
    def __init__(self, flux_handle, fd_int, events, callback, args=None):
        self.flux_handle = flux_handle
        self.fd_int = fd_int
        self.events = events
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(FDWatcher, self).__init__(
            raw.flux_fd_watcher_create(
                raw.flux_get_reactor(flux_handle),
                self.fd_int,
                self.events,
                lib.fd_handler_wrapper,
                self.wargs,
            )
        )
