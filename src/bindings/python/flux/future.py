###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
from typing import Dict

from flux.core.inner import ffi, lib, raw
from flux.util import check_future_error, interruptible
from flux.wrapper import Wrapper, WrapperPimpl

# Reference count dictionary to keep futures with a pending `then` callback
# alive, even if there are no remaining references to the future in the user's
# program scope. When a callback is first set on the future, add an entry to the
# dictionary with the future itself as the key and a value of 1. Whenever a
# future's callback is run, decrement the count in the dictionary for that
# future, and whenever reset is called on a future, increment the counter. If
# the counter hits 0, delete the reference to the future from the dictionary.
_THEN_HANDLES: Dict["Future", int] = {}


@ffi.def_extern()
def continuation_callback(c_future, opaque_handle):
    try:
        py_future: "Future" = ffi.from_handle(opaque_handle)
        assert c_future == py_future.pimpl.handle
        if not py_future.stopped:
            py_future.then_cb(py_future, *py_future.then_args, **py_future.then_kwargs)
    # pylint: disable=broad-except
    except Exception as exc:
        #
        # Uncaught exceptions stop reactor with error.
        # Setting the exception in the global Flux handle class will cause
        #  the handle to re-throw the exception after leaving the reactor:
        #
        flux_handle = py_future.get_flux()
        type(flux_handle).set_exception(exc)
        flux_handle.reactor_stop_error()

    finally:
        _THEN_HANDLES[py_future] -= 1
        if _THEN_HANDLES[py_future] <= 0:
            # allow future object to be garbage collected now that all
            # registered callbacks have completed
            py_future.cb_handle = None
            del _THEN_HANDLES[py_future]


class Future(WrapperPimpl):
    """
    A wrapper for interfaces that create and consume flux futures
    """

    class InnerWrapper(Wrapper):
        def __init__(
            self,
            handle=None,
            match=ffi.typeof("flux_future_t *"),
            filter_match=True,
            prefixes=None,
            destructor=raw.flux_future_destroy,
        ):
            # avoid using a static list as a default argument
            # pylint error 'dangerous-default-value'
            if prefixes is None:
                prefixes = ["flux_future_"]

            # enable Future(fut_obj) to work by just re-wrapping the
            # underlying future object and increments its reference to avoid
            # pre-mature destruction when the original obj gets GC'd
            if isinstance(handle, Future):
                handle.incref()
                handle = handle.handle

            super(Future.InnerWrapper, self).__init__(
                ffi, lib, handle, match, filter_match, prefixes, destructor
            )

        def check_wrap(self, fun, name):
            func = super(Future.InnerWrapper, self).check_wrap(fun, name)
            return check_future_error(func)

    def __init__(self, future_handle, prefixes=None, pimpl_t=None):
        super(Future, self).__init__()
        if pimpl_t is None:
            pimpl_t = self.InnerWrapper
        self.pimpl = pimpl_t(handle=future_handle, prefixes=prefixes)
        self.then_cb = None
        self.then_args = []
        self.then_kwargs = {}
        self.cb_handle = None
        self.stopped = False

    def stop(self):
        """Stop a future from calling the user callback.
        Useful for streaming futures given lack of destroy.
        """
        self.stopped = True

    def error_string(self):
        try:
            errmsg = self.pimpl.error_string()
        except EnvironmentError:
            return None
        return errmsg.decode("utf-8") if errmsg else None

    def get_flux(self):
        # importing within a function to break a cyclic import
        # pylint: disable=cyclic-import, import-outside-toplevel
        import flux.core.handle

        try:
            flux_handle = self.pimpl.get_flux()
        except OSError as exc:
            #  get_flux() throws OSError of EINVAL if !f->h, but this should
            #   a valid return (i.e. no flux handle set yet)
            if exc.errno == errno.EINVAL:
                return None
            raise
        if flux_handle == ffi.NULL:
            return None
        handle = flux.core.handle.Flux(handle=flux_handle)
        # increment reference count to prevent destruction of the underlying handle
        # (which is owned by the future) when the flux handle is garbage collected
        handle.incref()
        return handle

    def get_reactor(self):
        return self.pimpl.get_reactor()

    def then(self, callback, *args, timeout=-1.0, **kwargs):
        if self in _THEN_HANDLES:
            raise EnvironmentError(
                errno.EEXIST, "then callback already exists for this future"
            )
        if callback is None:
            raise ValueError("Callback cannot be None")

        self.then_cb = callback
        self.then_args = args
        self.then_kwargs = kwargs
        self.cb_handle = ffi.new_handle(self)
        self.pimpl.then(timeout, lib.continuation_callback, self.cb_handle)

        # ensure that this future object is not garbage collected with a
        # callback outstanding. Particularly important for anonymous calls and
        # streaming RPCs.  For example, `f.rpc('topic').then(cb)`
        _THEN_HANDLES[self] = 1

        # return self to enable further chaining of the future.
        # For example `f.rpc('topic').then(cb).wait_for(-1)
        return self

    def reset(self):
        self.pimpl.reset()

        if self.cb_handle is not None:
            # ensure that this future object is not garbage collected with a
            # callback outstanding. Particularly important for streaming RPCs.
            _THEN_HANDLES[self] += 1

    def is_ready(self):
        return self.pimpl.is_ready()

    @interruptible
    def wait_for(self, timeout=-1.0):
        self.pimpl.wait_for(timeout)
        return self

    @interruptible
    def get(self):
        """
        Base Future.get() method. Does not return a result, just blocks
        until future is fulfilled and throws OSError on failure.
        """
        self.pimpl.flux_future_get(ffi.NULL)

    def incref(self):
        self.pimpl.flux_future_incref()


class WaitAllFuture(Future):
    """Create a composite future which waits for all children to be fulfilled"""

    def __init__(self, children=None):
        self.children = children
        if self.children is None:
            self.children = []
        future = raw.flux_future_wait_all_create()
        super(WaitAllFuture, self).__init__(future)

    def push(self, child, name=None):
        if name is None:
            name = ffi.NULL
        #
        #  if this future does not have a flux handle yet, attempt
        #   to grab from the first pushed "child" future.
        if self.get_flux() is None:
            self.pimpl.set_flux(child.get_flux())

        self.pimpl.push(name, child)
        #
        #  flux_future_push(3) "adopts" memory for child, so call
        #   incref on child to avoid  calling flux_future_destroy(3) when
        #   child goes out of scope in caller context.
        child.pimpl.incref()
        self.children.append(child)
