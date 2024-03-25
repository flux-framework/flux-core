###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import signal
import threading
from contextlib import contextmanager

from _flux._core import ffi, lib
from flux.constants import FLUX_POLLERR, FLUX_POLLIN, FLUX_POLLOUT
from flux.core.inner import raw
from flux.core.watchers import FDWatcher, SignalWatcher, TimerWatcher
from flux.future import Future
from flux.message import Message, MessageWatcher
from flux.rpc import RPC
from flux.util import encode_payload, encode_topic
from flux.wrapper import Wrapper


# pylint: disable=too-many-public-methods
class Flux(Wrapper):
    """
    The general Flux handle class, create one of these to connect to the
    nearest enclosing flux instance

    Example:
        >>> flux.Flux()
        <flux.core.Flux object at 0x...>
    """

    #  Thread local storage to hold a reactor_running boolean, indicating
    #  when the current thread is running under a Flux reactor.
    #
    tls = threading.local()
    tls.reactor_depth = 0
    tls.exception = None

    def __init__(self, url=ffi.NULL, flags=0, handle=None):
        super(Flux, self).__init__(
            ffi,
            lib,
            handle=handle,
            match=ffi.typeof(lib.flux_open).result,
            filter_match=False,
            prefixes=["flux_", "FLUX_"],
            destructor=raw.flux_close,
        )

        # Ensure reactor_depth is initialized for this thread
        if "reactor_depth" not in self.tls.__dict__:
            self.tls.reactor_depth = 0
            self.tls.exception = None

        if handle is None:
            error = ffi.new("flux_error_t[1]")
            try:
                self.handle = raw.flux_open_ex(url, flags, error)
            except OSError as err:
                msg = "Unable to connect to Flux"
                errmsg = ffi.string(error[0].text).decode("utf-8")
                if errmsg:
                    msg += f": {errmsg}"
                raise OSError(err.errno, msg)

        self.aux_txn = None
        self._active_watchers = set()

    @classmethod
    def reactor_running(cls):
        """Return True if this thread is running the Flux reactor"""
        return cls.tls.reactor_depth > 0

    @classmethod
    def reactor_enter(cls):
        cls.tls.reactor_depth += 1

    @classmethod
    def reactor_exit(cls):
        cls.tls.reactor_depth -= 1

    @contextmanager
    def in_reactor(self):
        self.reactor_enter()
        try:
            yield
        finally:
            self.reactor_exit()

    @classmethod
    def set_exception(cls, exception):
        """Set a global, per-thread exception for Flux

        This class method allows Python callbacks called from the Flux
        reactor to set a global exception which can be re-thrown after
        the return to Python (when reactor_run() returns). This is
        implemented as a class attribute since the Flux handle object
        which is available in a Python callback from C will be a
        different instantiation than the Flux handle object which
        started the reactor (with the same underlying flux_t however)

        Args:
            exception (Exception): A reference to the exception thrown.

        Returns:
            Exception: The previously set exception, or None
        """
        prev = cls.tls.exception
        cls.tls.exception = exception
        return prev

    @classmethod
    def raise_if_exception(cls):
        """Re-raise any class global exception if set

        If a global exception is currently set for the Flux handle class,
        re-raise it and reset the exception state to None.

        The exception is raised ``from None`` to preserve the original
        stack trace.
        """
        #  Note: for unknown reason, an AttributeError is occasionally raised
        #  here: '_thread._local' object has no attribute 'exception'
        #  This should not be possible since the attribute is initialized
        #  in the class, but workaround it anyway through use of getattr():
        #
        if getattr(cls.tls, "exception", None) is not None:
            raise cls.set_exception(None) from None

    # pylint: disable=no-self-use
    def close(self):
        """
        The underlying flux handle is automatically closed when a Flux instance is
        deconstructed.  Prevent users from manually closing, the handle, leading
        to a double free.
        """
        raise RuntimeError(
            "Unnecessary manual invocation of a Flux handle's close method via "
            "the python bindings.  Handles are automatically closed when the "
            "Python object is deconstructed."
        )

    def log(self, level, fstring):
        """
        Log to the flux logging facility

        :param level: A syslog log-level, check the syslog module for possible
               values
        :param fstring: A string to log, C-style formatting is *not* supported
        """
        # Short-circuited because variadics can't be wrapped cleanly
        if isinstance(fstring, str):
            fstring = fstring.encode("utf-8")
        lib.flux_log(self.handle, level, fstring)

    def send(self, message, flags=0):
        """Send a pre-constructed flux message"""
        if isinstance(message, Message):
            message = message.handle
        return self.flux_send(message, flags)

    def respond(self, message, payload=None):
        """Respond to a flux rpc

        :param message: The message to respond to
        :type message: Message
        :param payload: The (optional) payload to include in the response
        :type payload: None, str, bytes, unicode, or json-serializable
        """
        if isinstance(message, Message):
            message = message.handle
        payload = encode_payload(payload)
        return self.flux_respond(message, payload)

    def recv(
        self,
        type_mask=raw.FLUX_MSGTYPE_ANY,
        match_tag=raw.FLUX_MATCHTAG_NONE,
        topic_glob=None,
        flags=0,
    ):
        """
        Receive a message, returns a flux.Message containing the result or None
        """
        match = ffi.new(
            "struct flux_match *",
            {
                "typemask": type_mask,
                "matchtag": match_tag,
                "topic_glob": topic_glob if topic_glob is not None else ffi.NULL,
            },
        )
        handle = self.flux_recv(match[0], flags)
        if handle is not None:
            return Message(handle=handle)
        return None

    def rpc(self, topic, payload=None, nodeid=raw.FLUX_NODEID_ANY, flags=0):
        """Create a new RPC object"""
        return RPC(self, topic, payload, nodeid, flags)

    def event_create(self, topic, payload=None):
        """Create a new event message.

        :param topic: A string, the event's topic
        :param payload: If a string, the payload is used unmodified, if it is
            another type json.dumps() is used to stringify it
        """
        # pylint: disable=no-self-use
        return Message.from_event_encode(topic, payload)

    def event_send(self, topic, payload=None):
        """Create and send a new event in one step"""
        return self.send(self.event_create(topic, payload))

    def event_recv(self, topic=None):
        return self.recv(type_mask=raw.FLUX_MSGTYPE_EVENT, topic_glob=topic)

    def event_subscribe(self, topic):
        """Subscribe to events

        :param topic: The event's topic to subscribe to
        :type topic: str, bytes, or unicode
        :raises EnvironmentError: if the topic is None or NULL
        :raises TypeError: if the topic is not a str, bytes, or unicode
        """
        return self.flux_event_subscribe(encode_topic(topic))

    def add_watcher(self, watcher):
        """Add a reference to a watcher so it avoids garbage collection"""
        self._active_watchers.add(watcher)
        return watcher

    def del_watcher(self, watcher):
        """Remove ref to ``watcher`` so it is eligible for garbage collection"""
        self._active_watchers.discard(watcher)

    def msg_watcher_create(
        self,
        callback,
        type_mask=raw.FLUX_MSGTYPE_ANY,
        topic_glob="*",
        args=None,
        match_tag=raw.FLUX_MATCHTAG_NONE,
        rolemask=None,
    ):
        return MessageWatcher(
            self, type_mask, callback, topic_glob, match_tag, rolemask, args
        )

    def timer_watcher_create(self, after, callback, repeat=0.0, args=None):
        return TimerWatcher(self, after, callback, repeat=repeat, args=args)

    def signal_watcher_create(self, signum, callback, args=None):
        return SignalWatcher(self, signum, callback, args)

    def fd_watcher_create(self, fd_int, callback, events=None, args=None):
        if events is None:
            # TODO add mypy type stubs for constants so this passes vermin without
            # comment
            events = FLUX_POLLIN | FLUX_POLLOUT | FLUX_POLLERR  # novm
        return FDWatcher(self, fd_int, events, callback, args=args)

    def barrier(self, name, nprocs):
        return Future(self.flux_barrier(name, nprocs))

    def get_rank(self):
        rank = ffi.new("uint32_t [1]")
        self.flux_get_rank(rank)
        return rank[0]

    def attr_get(self, attr_name):
        return self.flux_attr_get(attr_name).decode("utf-8")

    def reactor_run(self, reactor=None, flags=0):
        """
        Run reactor associated with this Flux handle or reactor argument
        if it is provided. Sets a signal watcher for SIGINT to return
        from the reactor on Ctrl-C, and raise KeyboardInterrupt.
        """
        rc = 0
        if reactor is None:
            reactor = self.get_reactor()

        #
        #  Only do the whole signals rigamarole below if we're in the
        #   the main thread: libev don't take kindly to registration
        #   of signal watcher from multiple threads.
        #
        if threading.current_thread() != threading.main_thread():
            rc = self.flux_reactor_run(reactor, flags)
            Flux.raise_if_exception()
            return rc

        reactor_interrupted = False

        def reactor_interrupt(handle, *_args):
            #  ensure reactor_interrupted from enclosing scope:
            nonlocal reactor_interrupted
            reactor_interrupted = True
            handle.reactor_stop(reactor)

        with self.signal_watcher_create(signal.SIGINT, reactor_interrupt):
            with self.in_reactor():
                # This signal watcher should not take a reference on reactor
                #  o/w the reactor may not exit as expected when all other
                #  active watchers and msghandlers are complete.
                #
                self.reactor_active_decref(reactor)
                rc = self.flux_reactor_run(reactor, flags)
                #  Re-establish signal watcher reference so reactor refcount
                #   doesn't underflow when signal watcher is destroyed
                #
                self.reactor_active_incref(reactor)
            if reactor_interrupted:
                raise KeyboardInterrupt
            Flux.raise_if_exception()

        # If rc > 0, we need to subtract our added SIGINT watcher, which
        # will now be destroyed since it has left scope
        return rc if rc <= 0 else rc - 1

    def reactor_stop(self, reactor=None):
        if reactor is None:
            reactor = self.get_reactor()
        self.flux_reactor_stop(reactor)

    def reactor_stop_error(self, reactor=None):
        if reactor is None:
            reactor = self.get_reactor()
        self.flux_reactor_stop_error(reactor)

    def reactor_incref(self, reactor=None):
        if reactor is None:
            reactor = self.get_reactor()
        self.reactor_active_incref(reactor)

    def reactor_decref(self, reactor=None):
        if reactor is None:
            reactor = self.get_reactor()
        self.reactor_active_decref(reactor)

    def service_register(self, name):
        return Future(self.flux_service_register(name))

    def service_unregister(self, name):
        return Future(self.flux_service_unregister(name))


# vi: ts=4 sw=4 expandtab
