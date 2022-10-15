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
import errno
import signal

from flux.core.inner import ffi, lib, raw

__all__ = ["TimerWatcher", "FDWatcher", "SignalWatcher"]


class Watcher(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self, flux_handle, handle=None):
        self.flux_handle = flux_handle
        self.flux_handle.add_watcher(self)
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
        self.destroy()

    def start(self):
        raw.flux_watcher_start(self.handle)
        return self

    def stop(self):
        raw.flux_watcher_stop(self.handle)
        return self

    def destroy(self):
        #  Remove this watcher from its owning Flux handle
        #  if the handle is still around. A try/except block
        #  used here in case the Watcher got an exception
        #  before __init__ (e.g. in subclass __init__)
        try:
            self.flux_handle.del_watcher(self)
        except AttributeError:
            pass
        if self.handle is not None:
            raw.flux_watcher_destroy(self.handle)
            self.handle = None


@ffi.def_extern()
def timeout_handler_wrapper(unused1, unused2, revents, opaque_handle):
    del unused1, unused2  # unused arguments
    watcher = ffi.from_handle(opaque_handle)
    try:
        watcher.callback(watcher.flux_handle, watcher, revents, watcher.args)
    # pylint: disable=broad-except
    except Exception as exc:
        type(watcher.flux_handle).set_exception(exc)
        watcher.flux_handle.reactor_stop_error()


class TimerWatcher(Watcher):
    def __init__(self, flux_handle, after, callback, repeat=0, args=None):
        self.after = after
        self.repeat = repeat
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(TimerWatcher, self).__init__(
            flux_handle,
            raw.flux_timer_watcher_create(
                raw.flux_get_reactor(flux_handle),
                float(after),
                float(repeat),
                lib.timeout_handler_wrapper,
                self.wargs,
            ),
        )


@ffi.def_extern()
def fd_handler_wrapper(unused1, unused2, revents, opaque_handle):
    del unused1, unused2  # unused arguments
    watcher = ffi.from_handle(opaque_handle)
    try:
        fd_int = raw.fd_watcher_get_fd(watcher.handle)
        watcher.callback(watcher.flux_handle, watcher, fd_int, revents, watcher.args)
    # pylint: disable=broad-except
    except Exception as exc:
        type(watcher.flux_handle).set_exception(exc)
        watcher.flux_handle.reactor_stop_error()


class FDWatcher(Watcher):
    def __init__(self, flux_handle, fd_int, events, callback, args=None):
        self.fd_int = fd_int
        self.events = events
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(FDWatcher, self).__init__(
            flux_handle,
            raw.flux_fd_watcher_create(
                raw.flux_get_reactor(flux_handle),
                self.fd_int,
                self.events,
                lib.fd_handler_wrapper,
                self.wargs,
            ),
        )


@ffi.def_extern()
def signal_handler_wrapper(_unused1, _unused2, _unused3, opaque_handle):
    watcher = ffi.from_handle(opaque_handle)
    try:
        signal_int = raw.signal_watcher_get_signum(watcher.handle)
        watcher.callback(watcher.flux_handle, watcher, signal_int, watcher.args)
    # pylint: disable=broad-except
    except Exception as exc:
        type(watcher.flux_handle).set_exception(exc)
        watcher.flux_handle.reactor_stop_error()


class SignalWatcher(Watcher):
    def __init__(self, flux_handle, signal_int, callback, args=None):
        self.signal_int = signal_int
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(SignalWatcher, self).__init__(
            flux_handle,
            raw.flux_signal_watcher_create(
                raw.flux_get_reactor(flux_handle),
                self.signal_int,
                lib.signal_handler_wrapper,
                self.wargs,
            ),
        )
        # N.B.: check for error only after SignalWatcher object fully
        #  initialized to avoid 'no attribute self.handle' in __del__
        #  method.
        if signal_int < 1 or signal_int >= signal.NSIG:
            raise OSError(errno.EINVAL, "invalid signal number")


# vi: ts=4 sw=4 expandtab
