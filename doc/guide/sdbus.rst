.. _sdbus:

###################
D-Bus Bridge Module
###################

The sdbus broker module bridges Flux RPCs to D-Bus, allowing other modules
to communicate with systemd without managing a D-Bus connection directly.
One instance runs per broker rank.

sdbus connects to :envvar:`DBUS_SESSION_BUS_ADDRESS`, which the Flux systemd
unit file sets to the user-level systemd D-Bus socket of the flux user.

Connection and I/O
==================

sdbus uses libsystemd at a low level to operate reactively without busy-polling.
The D-Bus file descriptor is obtained via :linux:man3:`sd_bus_get_fd` and
registered with the Flux reactor.  The watcher callback calls
:linux:man3:`sd_bus_process` to receive one message per invocation.

Received messages are dispatched by type: method replies and method errors are
matched to pending requests by cookie; signals are matched against active
subscriptions.

Reconnection
============

If the D-Bus connection is lost, sdbus queues new requests and reconnects with
exponential backoff (minimum 2 seconds, maximum 60 seconds).  Existing
subscribers are notified of the disconnect via ``EAGAIN``.  When the connection
is restored, queued requests are drained.

Protocol
========

The ``sdbus.call`` and ``sdbus.subscribe`` RPCs, and the JSON encoding of
D-Bus message bodies they carry, are defined in :rfc:`52`.  Requests carry
the full D-Bus address and type signature of each method call, and responses
carry the signature of each reply or signal, so sdbus needs no knowledge of
the methods being called.  ``message.c`` translates any D-Bus type, walking
the type signature to convert JSON to D-Bus, and walking the message itself
to convert D-Bus to JSON.

Because sdbus operates at a low level, it handles method-reply/method-error
matching itself rather than delegating to the higher-level libsystemd helpers
that would normally do this.

Using sdbus with systemd
========================

:rfc:`52` is not specific to systemd.  The following notes apply when the
peer is the systemd manager.

Addressing
   Manager methods such as StartTransientUnit use destination
   org.freedesktop.systemd1, path /org/freedesktop/systemd1, and
   interface org.freedesktop.systemd1.Manager.  Unit properties are read
   with the Get and GetAll methods of interface
   org.freedesktop.DBus.Properties on the unit's object path, naming a
   unit interface such as org.freedesktop.systemd1.Service as the first
   argument.  libsdexec defines these names in ``bus.h``.

Unit object paths
   Each unit has an object path under /org/freedesktop/systemd1/unit/
   derived from its name, as described in :ref:`libsdexec_object_paths`.
   sdbus carries object paths verbatim, so callers that start from a unit
   name must encode it, and callers that receive a unit path must decode it.

Signals
   systemd emits unit signals such as PropertiesChanged only to clients
   that have called the manager's Subscribe method.  sdbus calls
   Subscribe, then AddMatch with ``type=signal``, each time it
   connects, so clients need only send ``sdbus.subscribe``.

64-bit values
   Many unit properties have type *t*, which is encoded as a decimal
   string.  systemd uses UINT64_MAX, encoded as
   ``"18446744073709551615"``, to mean "infinity" or "unset", for example
   in MemoryMax.  Timestamps are in microseconds.

Transient unit properties
   The third argument of StartTransientUnit has signature ``a(sv)``, an
   array of [name, [signature, value]] pairs, for example
   ``["MemoryMax", ["t", "18446744073709551615"]]``.  The signature of each
   property is given in the systemd D-Bus documentation and introspection
   files (see `Exploring the D-Bus Interface`_).

File descriptors
   The StandardInputFileDescriptor, StandardOutputFileDescriptor,
   and StandardErrorFileDescriptor properties have type *h*, an
   object like ``{"fd": 5, "pid": 1234}``.  As required by :rfc:`52`,
   sdbus rejects an *h* value unless *pid* matches its own process id,
   so these properties can only be sent by sdexec, which runs in the same
   broker process as sdbus.  They are write-only: Get and GetAll return
   related string properties such as StandardInputFileDescriptorName
   instead, so unit property replies never carry *h* values, which
   :rfc:`52` does not allow to be encoded.

Errors
   systemd method errors carry D-Bus error names, which sdbus includes in the
   error string, for example
   ``org.freedesktop.systemd1.NoSuchUnit: Unit foo.service not loaded.``

Exploring the D-Bus Interface
=============================

The systemd D-Bus object hierarchy can be browsed live with :linux:man1:`busctl`.
Use ``tree`` to list object paths under the systemd service::

   busctl tree org.freedesktop.systemd1

To list all properties of a running unit with their D-Bus type signatures, use
``introspect`` with the unit's object path (see
:ref:`libsdexec_object_paths`).  GetUnit returns the object path of a
loaded unit::

   busctl --user call org.freedesktop.systemd1 /org/freedesktop/systemd1 \
       org.freedesktop.systemd1.Manager GetUnit s some.service



   busctl introspect org.freedesktop.systemd1 \
       /org/freedesktop/systemd1/unit/some_2eservice \
       org.freedesktop.systemd1.Service

The same information is available statically in the D-Bus introspection XML
files installed by the systemd-dev package under
``/usr/share/dbus-1/interfaces/``, one file per interface.  These files are
the authoritative source for property type signatures used in
StartTransientUnit calls.

******************
External Resources
******************

- :rfc:`52` — D-Bus Bridge Protocol
- `D-Bus specification <https://dbus.freedesktop.org/doc/dbus-specification.html>`_
- `The new sd-bus API of systemd <https://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html>`_
- `org.freedesktop.systemd1 D-Bus interface <https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.systemd1.html>`_
- :linux:man5:`systemd.resource-control` — resource control properties including DeviceAllow
