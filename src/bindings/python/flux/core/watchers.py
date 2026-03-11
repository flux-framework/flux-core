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

__all__ = ["TimerWatcher", "FDWatcher", "SignalWatcher", "CheckWatcher", "IdleWatcher"]


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

    def ref(self):
        raw.flux_watcher_ref(self.handle)
        return self

    def unref(self):
        raw.flux_watcher_unref(self.handle)
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

    def reset(self, after=None, repeat=None):
        """Reset the timer and restart it.

        Updates the timer values and re-arms it from scratch, even if it is
        already running.  Parameters default to the values passed at creation.
        """
        if after is None:
            after = self.after
        if repeat is None:
            repeat = self.repeat
        raw.flux_timer_watcher_reset(self.handle, float(after), float(repeat))
        self.start()


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


class PrepareWatcher(Watcher):
    """Watcher that fires once per reactor iteration, before blocking.

    Fires just before the event loop calls ``backend_poll``.  Typically
    paired with an :class:`IdleWatcher` (no callback) and a
    :class:`CheckWatcher`: if work is pending, ``prepare_cb`` starts the
    idle watcher to prevent blocking; ``check_cb`` does the work and stops
    the idle watcher.
    """

    def __init__(self, flux_handle, callback, args=None):
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(PrepareWatcher, self).__init__(
            flux_handle,
            raw.flux_prepare_watcher_create(
                raw.flux_get_reactor(flux_handle),
                lib.timeout_handler_wrapper,
                self.wargs,
            ),
        )


class CheckWatcher(Watcher):
    """Watcher that fires once per reactor iteration, after blocking.

    Fires just after the event loop returns from ``backend_poll``, before
    I/O callbacks from the current poll are dispatched.  Typically paired
    with a :class:`PrepareWatcher` and an :class:`IdleWatcher`; see
    :class:`PrepareWatcher` for the pattern.
    """

    def __init__(self, flux_handle, callback, args=None):
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        super(CheckWatcher, self).__init__(
            flux_handle,
            raw.flux_check_watcher_create(
                raw.flux_get_reactor(flux_handle),
                lib.timeout_handler_wrapper,
                self.wargs,
            ),
        )


class IdleWatcher(Watcher):
    """Watcher that prevents the event loop from blocking.

    When active, keeps ``backend_poll`` from sleeping so that the loop
    spins without waiting for I/O.  Typically used without a callback (pass
    ``callback=None``) as part of the prepare/idle/check pattern: the
    prepare watcher starts this watcher when there is work pending, and the
    check watcher stops it after the work is done.
    """

    def __init__(self, flux_handle, callback=None, args=None):
        self.callback = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self) if callback is not None else ffi.NULL
        super(IdleWatcher, self).__init__(
            flux_handle,
            raw.flux_idle_watcher_create(
                raw.flux_get_reactor(flux_handle),
                lib.timeout_handler_wrapper if callback is not None else ffi.NULL,
                self.wargs,
            ),
        )


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
