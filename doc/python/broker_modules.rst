.. _python_broker_modules:

Python Broker Modules
=====================

Flux broker modules extend the broker with new services.  Traditionally
modules are written in C as dynamic shared objects, but Flux also supports
modules written in Python.  A Python broker module is a :file:`.py` file that
defines either a :func:`mod_main` entry function or a single
:class:`~flux.brokermod.BrokerModule` subclass.


Loading a Python module
-----------------------

Python modules are loaded and removed with the same :man1:`flux-module`
commands used for C modules:

.. code-block:: console

   $ flux module load /path/to/mymod.py
   $ flux ping mymod
   $ flux module remove mymod

The module name defaults to the file's basename without the ``.py`` suffix
and may be overridden with ``--name``:

.. code-block:: console

   $ flux module load --name myservice /path/to/mymod.py


The mod_main entry point
------------------------

A Python broker module may define a top-level function named :func:`mod_main`:

.. code-block:: python

   def mod_main(h, *args):
       ...

``h`` is a :class:`flux.Flux` handle connected to the broker.  ``args``
is a (possibly empty) tuple of strings passed after the module path on the
:program:`flux module load` command line.  The function must run the reactor
(directly or via :class:`~flux.brokermod.BrokerModule`) and return only after
the reactor exits.

Defining :func:`mod_main` explicitly is optional when using
:class:`~flux.brokermod.BrokerModule` — see `Using BrokerModule`_ below.


Using BrokerModule
------------------

:class:`~flux.brokermod.BrokerModule` is a base class that handles the
boilerplate of registering message handlers, managing the reactor, and
looking up the module name.  Subclass it and use the
:func:`~flux.brokermod.request_handler` and
:func:`~flux.brokermod.event_handler` decorators to declare handlers:

.. code-block:: python

   from flux.brokermod import BrokerModule, event_handler, request_handler


   class MyModule(BrokerModule):

       @request_handler("info")
       def info(self, msg):
           self.handle.respond(msg, {"name": self.name})

       @event_handler("shutdown", prefix=False)
       def on_shutdown(self, msg):
           self.stop()

When a module file contains exactly one :class:`~flux.brokermod.BrokerModule`
subclass and no :func:`mod_main`, the loader synthesizes the entry point
automatically as ``MyModule(h, *args).run()``, so no explicit
:func:`mod_main` is required.  If multiple subclasses are present,
:func:`mod_main` must be defined to resolve the ambiguity:

.. code-block:: python

   def mod_main(h, *args):
       MyModule(h, *args).run()

Handler methods receive a :class:`~flux.message.Message` object.  For request
handlers, call :meth:`flux.Flux.respond` on the handle to send a response.

By default each handler's topic is prefixed with the module name, so
``@request_handler("info")`` registers ``mymod.info``.  Pass
``prefix=False`` to use the topic string as-is.

By default, only requests from the instance owner are delivered to a handler.
Pass ``allow_guest=True`` to also accept requests from non-owner users:

.. code-block:: python

   @request_handler("status", allow_guest=True)
   def status(self, msg):
       self.handle.respond(msg, {"running": True})

The :attr:`~flux.brokermod.BrokerModule.handle` property returns the
:class:`flux.Flux` handle, giving full access to the Flux Python API for
RPCs, KVS operations, timers, and other watchers.


Error handling
--------------

If :meth:`~flux.brokermod.BrokerModule.run` raises :exc:`OSError`, or if
:func:`mod_main` raises any exception, the broker treats the module as
having exited with an error and logs a ``module runtime failure`` message.
Use :meth:`~flux.brokermod.BrokerModule.stop_error` to signal an error from
within a handler.


Module arguments
----------------

Arguments passed after the module path at load time are available as
``args`` in :func:`mod_main` and as
:attr:`~flux.brokermod.BrokerModule.args` on a :class:`~flux.brokermod.BrokerModule`
instance:

.. code-block:: console

   $ flux module load /path/to/mymod.py --verbose --count=4

.. code-block:: python

   def mod_main(h, *args):
       # args == ('--verbose', '--count=4')
       MyModule(h, *args).run()

Arguments are split on whitespace; quoting is not supported.


API reference
-------------

.. autoclass:: flux.brokermod.BrokerModule
   :members:
   :undoc-members:

.. autofunction:: flux.brokermod.request_handler

.. autofunction:: flux.brokermod.event_handler
