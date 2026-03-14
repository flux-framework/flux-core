###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from _flux._core import ffi, lib
from flux.constants import FLUX_MSGTYPE_EVENT, FLUX_MSGTYPE_REQUEST


def request_handler(topic, prefix=True, allow_guest=False):
    """Decorator to register a method as a request handler.

    The handler is invoked with (self, msg) when a request matching
    ``<module_name>.<topic>`` is received.  If ``prefix=False``, the topic
    is used as-is without prepending the module name.  If ``allow_guest=True``,
    requests from non-owner users (``FLUX_ROLE_USER``) are also accepted.
    """

    def decorator(func):
        func._flux_handler = {
            "msgtype": FLUX_MSGTYPE_REQUEST,
            "topic": topic,
            "prefix": prefix,
            "allow_guest": allow_guest,
        }
        return func

    return decorator


def event_handler(topic, prefix=True):
    """Decorator to register a method as an event handler.

    The handler is invoked with (self, msg) when an event matching
    ``<module_name>.<topic>`` is received.  If ``prefix=False``, the topic
    is used as-is without prepending the module name.
    """

    def decorator(func):
        func._flux_handler = {
            "msgtype": FLUX_MSGTYPE_EVENT,
            "topic": topic,
            "prefix": prefix,
        }
        return func

    return decorator


class BrokerModule:
    """Base class for Python broker modules.

    Subclass this and use the :func:`request_handler` and
    :func:`event_handler` decorators to register message handlers, then
    call :meth:`run` to start the reactor.  Handler topics are automatically
    prefixed with the module name unless ``prefix=False`` is passed to the
    decorator.

    Example::

        from flux.brokermod import BrokerModule, event_handler, request_handler

        class MyModule(BrokerModule):

            @request_handler("info")
            def info(self, msg):
                self.handle.respond(msg, self.name)

            @event_handler("panic")
            def panic(self, msg):
                self.stop_error()

    When this is the only :class:`BrokerModule` subclass in the file and no
    :func:`mod_main` is defined, the loader synthesizes the entry point
    automatically.  An explicit :func:`mod_main` is only needed to resolve
    ambiguity when multiple subclasses are present.
    """

    def __init__(self, h, *args):
        self._h = h
        self._args = args
        name_p = ffi.cast("char *", lib.flux_aux_get(h.handle, b"flux::name"))
        self._name = ffi.string(name_p).decode() if name_p != ffi.NULL else "unknown"
        self._watchers = []
        self._stopped_with_error = False
        self._register_handlers()

    @property
    def handle(self):
        """The underlying :class:`flux.Flux` handle."""
        return self._h

    @property
    def name(self):
        """The module name as assigned by the broker."""
        return self._name

    @property
    def args(self):
        """Tuple of module arguments passed at load time."""
        return self._args

    def set_running(self):
        """Signal to the broker that the module is now running.

        Call this before performing synchronous initialization in
        :meth:`run` to allow ``flux module load`` to return instead of
        waiting for the reactor to start.  See :man3:`flux_module_set_running`.
        """
        if lib.flux_module_set_running(self._h.handle) < 0:
            raise OSError("flux_module_set_running failed")

    def debug_test(self, flag, clear=False):
        """Test a module debug bit, optionally clearing it.

        Returns True if the bit identified by *flag* is set.  If *clear* is
        True the bit is atomically cleared after being read.  Bits are set
        externally via ``flux module debug --setbit N <module-name>``.
        """
        return lib.flux_module_debug_test(self._h.handle, flag, clear)

    def stop(self):
        """Stop the reactor and exit :meth:`run` normally."""
        self._h.reactor_stop()

    def stop_error(self):
        """Stop the reactor and cause :meth:`run` to raise :exc:`OSError`."""
        self._stopped_with_error = True
        self._h.reactor_stop_error()

    def run(self):
        """Run the reactor until stopped.

        Raises :exc:`OSError` if the reactor was stopped with an error
        (e.g. via :meth:`stop_error`) or if a Python exception was set
        on the handle during a callback.
        """
        if self._h.reactor_run() < 0 or self._stopped_with_error:
            raise OSError("reactor exited with error")

    def _register_handlers(self):
        for cls in type(self).__mro__:
            for attr_name, func in cls.__dict__.items():
                spec = getattr(func, "_flux_handler", None)
                if spec is None:
                    continue
                method = getattr(self, attr_name)
                msgtype = spec["msgtype"]
                topic = (
                    f"{self._name}.{spec['topic']}" if spec["prefix"] else spec["topic"]
                )
                rolemask = None
                if spec.get("allow_guest"):
                    rolemask = lib.FLUX_ROLE_OWNER | lib.FLUX_ROLE_USER
                if msgtype == FLUX_MSGTYPE_EVENT:
                    self._h.event_subscribe(topic)

                def _make_cb(m):
                    def cb(handle, mtype, msg, arg):
                        m(msg)

                    return cb

                w = self._h.msg_watcher_create(
                    _make_cb(method), msgtype, topic, rolemask=rolemask
                )
                w.start()
                self._watchers.append(w)


def resolve_entry_point(mod):
    """Resolve the entry point callable from a Python broker module object.

    If the module defines :func:`mod_main` it is returned as-is.  Otherwise,
    a single :class:`BrokerModule` subclass is searched for in the module's
    namespace and its entry point synthesized as ``cls(h, *args).run()``.

    :raises ValueError: if no entry point can be determined, or if multiple
        :class:`BrokerModule` subclasses are found without an explicit
        :func:`mod_main`.
    """
    mod_main = getattr(mod, "mod_main", None)
    if mod_main is not None:
        return mod_main
    subclasses = [
        v
        for v in vars(mod).values()
        if isinstance(v, type) and issubclass(v, BrokerModule) and v is not BrokerModule
    ]
    if len(subclasses) == 1:
        cls = subclasses[0]
        return lambda h, *args: cls(h, *args).run()
    if len(subclasses) > 1:
        names = ", ".join(c.__name__ for c in subclasses)
        raise ValueError(
            f"multiple BrokerModule subclasses found ({names}); define mod_main()"
        )
    raise ValueError("no mod_main() or BrokerModule subclass found")


# vi: ts=4 sw=4 expandtab
