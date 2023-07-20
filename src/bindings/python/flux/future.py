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
import json
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

# Same as above, but for FutreExt init callbacks:
_INIT_HANDLES: Dict["Future", int] = {}


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
        #
        # Reset this future so it doesn't immediately call the callback
        # again (and fail) if the reactor is restarted. (and bypass
        # future.reset() to avoid unnecessarily incrementing _THEN_HANDLES
        # count).
        #
        py_future.pimpl.reset()
        py_future.stop()

    finally:
        if py_future in _THEN_HANDLES:
            _THEN_HANDLES[py_future] -= 1
            if _THEN_HANDLES[py_future] <= 0:
                #
                #  Note, a multiply fulfilled future which is not reset
                #  before leaving the then_cb() will end up here, since a
                #  call to reset() is the only thing that increments the
                #  _THEN_HANDLES counter. If py_future.cb_handle is set
                #  to None at this point, then the handle could be garbage
                #  collected immediately. If the Future is not also collected,
                #  then continuation_callback() might be called again with
                #  the same cb_handle, which is now invalid, and Python will
                #  abort. Therefore, leave cb_handle defined for the
                #  lifetime of the Future.
                #
                del _THEN_HANDLES[py_future]


@ffi.def_extern()
def init_callback(c_future, opaque_handle):
    try:
        handle_ptr: "list" = ffi.from_handle(opaque_handle)
        future: "Future" = ffi.from_handle(handle_ptr[0])
        assert c_future == future.pimpl.handle
        future.init_cb(future, *future.init_args, **future.init_kwargs)
    # pylint: disable=broad-except
    except Exception as exc:
        flux_handle = future.get_flux()
        type(flux_handle).set_exception(exc)
        flux_handle.reactor_stop_error()
    finally:
        _INIT_HANDLES[future] -= 1
        if _INIT_HANDLES[future] <= 0:
            # allow future object to be garbage collected now that all
            # registered callbacks have completed
            future.init_handle = None
            del _INIT_HANDLES[future]


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

    def set_flux(self, flux_handle):
        self.pimpl.set_flux(flux_handle)

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

    def fulfill_error(self, errnum=2, errstr=ffi.NULL):
        self.pimpl.fulfill_error(errnum, errstr)

    def reset(self):
        self.pimpl.reset()

        if self in _THEN_HANDLES:
            # ensure that this future object is not garbage collected with a
            # callback outstanding. Particularly important for streaming RPCs.
            _THEN_HANDLES[self] += 1

    def is_ready(self):
        return self.pimpl.is_ready()

    def raise_if_handle_exception(self):
        #  Helper function to raise any pending exception on this Future's
        #  handle instead of raising an OSError returned from calling
        #  flux_future_get(3) or flux_future_wait_for(3). The reason for
        #  doing this is that these functions can fail if a Python callback
        #  raises an exception, since the callback could then return without
        #  fulfilling the target future, thus the low-level future calls
        #  can return EDEADLOCK. In other situations we also want to raise any
        #  pending exceptions since they will be more valuable than just
        #  the equivalent of an errno. (If the future was really fulfilled
        #  with an error, then no pending exception is expected, and the
        #  caller should go on to raise the original OSError exception.)
        #
        flux_handle = self.get_flux()
        if flux_handle is not None:
            type(flux_handle).raise_if_exception()

    @interruptible
    def wait_for(self, timeout=-1.0):
        try:
            self.pimpl.wait_for(timeout)
        except OSError:
            self.raise_if_handle_exception()
            raise

        return self

    @interruptible
    def get(self):
        """
        Base Future.get() method. Does not return a result, just blocks
        until future is fulfilled and throws OSError on failure.
        """
        try:
            self.pimpl.flux_future_get(ffi.NULL)
        except OSError:
            self.raise_if_handle_exception()
            raise

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


class FutureExt(Future):
    """
    Extensible Future for use directly from Python.

    This class allows creation of an "empty" Future which can then
    be fulfilled with a value or other Future directly from Python.

    The purpose is to allow multiple actions, RPCs, etc to be abstracted
    behind a single interface, with one "Future" object used to signal
    completion and/or availability of a result to the caller.
    """

    def __init__(self, init_cb, *args, flux_handle=None, **kw_args):
        """
        Create an empty Future with initialization callback init_cb(),
        with optional arguments args and kwargs.

        The initialization callback will be called either when a then()
        callback is registered on the returned Future or a blocking get()
        is used on the Future.

        The initialization callback should take care to use
        ``future.get_flux()`` to get the correct Flux handle for the
        context in which the initialization is running. This is because
        the underlying reactor will be different depending on whether
        the Future is being initialized in a blocking ``future.get()``
        or by a call to ``future.then()``. See the documentation for
        ``flux_future_create(3)`` for more information.

        If ``flux_handle`` is not None, then the given Flux handle will
        be associated with the Future during initialization. Otherwise,
        Future.set_flux() must be called before .then() or .get() are
        called, or an OSError with errno.EINVAL exception may be raised.

        Args:
            init_cb (Callable): Future initialization callback
            flux_handle (flux.Flux): Flux handle to associate with this Future
            args, kw_args: args and keyword args to pass along to init_cb
        """
        # The ffi handle for the Future object is not available until we
        # create it, but the ffi handle needs to be passed to
        # flux_future_create(3). Therefore, create a list object we can pass
        # to create(), then add the Future ffi handle to the list.
        #
        ffi_handle_list = []
        handle = ffi.new_handle(ffi_handle_list)

        # Setup initialization callback:
        self.init_handle = handle
        self.init_cb = init_cb
        self.init_args = args
        self.init_kwargs = kw_args

        # When this Future is fulfilled with a Python value, the memory for
        # the C copy of this value (or the string representation of it) must
        # have a reference held by this Future object, so it is not released
        # until this Future is garbage-collected. Use a _results list for this
        # purpose:
        self._results = []

        # Create a Future from flux_future_create(3)
        future = raw.flux_future_create(lib.init_callback, handle)

        # Place ffi handle for future into ffi_handle_list and set this
        # future in _INIT_HANDLES to avoid garbage collection
        ffi_handle_list.append(ffi.new_handle(self))
        _INIT_HANDLES[self] = 1
        super().__init__(future)

        if flux_handle is not None:
            self.set_flux(flux_handle)

    def fulfill(self, result=None):
        """
        Fulfill a future with a result. The ``result`` can be any object
        or value that is JSON serializable by json.dumps()
        """
        # Note: result=None handled since json.dumps(None) => 'null'
        result = json.dumps(result).encode("utf-8", errors="surrogateescape")
        payload = ffi.new("char[]", result)
        self.pimpl.fulfill(payload, ffi.NULL)

        # Python will free memory for the payload once it goes out of scope.
        # Therefore append payload to the internal results list to keep
        # a reference until the future is garbage collected.
        self._results.append(payload)

    @interruptible
    def get(self):
        """
        Convenience method to return a value stored by ``fulfill()``

        Note: will return garbage if future does not contain a string, i.e.
        if this Future was fulfilled outside of Python with a C object.

        Returns string payload or None if future payload is NULL.
        """
        payload = ffi.new("void **")
        try:
            self.pimpl.get(payload)
        except OSError:
            self.raise_if_handle_exception()
            raise

        if payload[0] == ffi.NULL:
            return None
        value = ffi.string(ffi.cast("char *", payload[0])).decode("utf-8")
        return json.loads(value)
