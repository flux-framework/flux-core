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

Message Translation
===================

Flux RPC payloads in JSON are translated to and from D-Bus messages.
D-Bus type signatures follow the `D-Bus specification
<https://dbus.freedesktop.org/doc/dbus-specification.html>`_.
The supported (interface, member) tuples and their type signatures
are enumerated in ``interface.c``.  Low-level type encoding and decoding is
in ``message.c``.  Both files should be amended when new D-Bus methods need
to be supported.

Because sdbus operates at a low level, it handles method-reply/method-error
matching itself rather than delegating to the higher-level libsystemd helpers
that would normally do this.

The following table shows how supported D-Bus types map to JSON.  All integer
types map to JSON integers regardless of signedness or width.  Object paths
are decoded from D-Bus hex-escape encoding to plain strings.  Variants are
represented as a two-element JSON array of ``[type_signature, value]``, and
property dictionaries (``a{sv}``) become JSON objects whose values are
variant arrays.

.. list-table::
   :header-rows: 1
   :widths: 25 10 65

   * - D-Bus type
     - Signature
     - JSON representation
   * - byte
     - y
     - integer
   * - boolean
     - b
     - true / false
   * - int16, int32, int64
     - n, i, x
     - integer
   * - uint16, uint32, uint64
     - q, u, t
     - integer
   * - double
     - d
     - number
   * - string
     - s
     - string
   * - object path
     - o
     - string (decoded via ``sd_bus_path_decode``; systemd-specific)
   * - signature
     - g
     - string
   * - unix fd
     - h
     - integer
   * - variant
     - v
     - ``["type", value]``, e.g. ``["s", "active"]``;
       ``a(ss)`` variants are also supported
   * - array of basic type
     - aX
     - JSON array, e.g. ``[1, 2, 3]`` for ``ai``
   * - array of string pairs
     - a(ss)
     - JSON array of two-element string arrays,
       e.g. ``[["/dev/nvidiactl", "rw"], ["/dev/nvidia0", "rw"]]``
       for ``DeviceAllow`` (device specifier, permissions)
   * - property dictionary
     - a{sv}
     - object, e.g. ``{"ActiveState": ["s", "active"]}``

The type translation covers the current systemd interface but not all D-Bus
types.  Complex variants with unknown content are decoded as JSON null rather
than failing explicitly.  Until a complete translation engine is implemented,
new D-Bus methods requiring unsupported types must be handled by manually
extending ``message.c`` and registering the new signature in ``interface.c``.

.. list-table::
   :header-rows: 1
   :widths: 50 10 10

   * - D-Bus type
     - Read
     - Write
   * - Basic types (y, b, n, q, i, u, x, t, h, d, s, g, o)
     - yes
     - yes
   * - Variant (v) with basic or simple array content, or a(ss) content
     - yes
     - yes
   * - Variant (v) with other complex content
     - null
     - yes
   * - Array of basic type (aX)
     - yes
     - yes
   * - Property dictionary (a{sv})
     - yes
     - no
   * - a(sv), a(sasb)
     - no
     - yes
   * - Arbitrary struct (...)
     - no
     - no
   * - Other dict and array signatures
     - no
     - no

RPC Interface
=============

.. object:: sdbus.call request

   Invoke a D-Bus method call.  Returns a single response when the reply
   arrives.

   member (str, required)
      D-Bus method name.

   params (array, required)
      Method arguments as a JSON array.

   interface (str)
      D-Bus interface.  Default: ``org.freedesktop.systemd1.Manager``.

   path (str)
      D-Bus object path.  Default: ``/org/freedesktop/systemd1``.

   destination (str)
      D-Bus service name.  Default: ``org.freedesktop.systemd1``.

.. object:: sdbus.call response

   params (array)
      Method return values as a JSON array.

.. object:: sdbus.subscribe request

   Subscribe to D-Bus signals.  Sends ``Subscribe`` followed by
   ``AddMatch`` on the bus connection, then streams one response per
   matching signal.  All fields are optional signal filters; omitting a
   field matches any value.

   path (str)
      Filter by D-Bus object path.

   interface (str)
      Filter by D-Bus interface name.

   member (str)
      Filter by signal member name.

.. object:: sdbus.subscribe response

   One response is streamed per matching signal.

   path (str)
      Object path of the signal source.

   interface (str)
      Interface name.

   member (str)
      Signal name.

   params (array)
      Signal parameters as a JSON array.


Exploring the D-Bus Interface
=============================

The systemd D-Bus object hierarchy can be browsed live with :linux:man1:`busctl`.
Use ``tree`` to list object paths under the systemd service::

   busctl tree org.freedesktop.systemd1

To list all properties of a running unit with their D-Bus type signatures, use
``introspect``.  Unit names are encoded as D-Bus object paths by replacing
special characters with their hex escape (e.g. ``.`` becomes ``_2e``)::

   busctl introspect org.freedesktop.systemd1 \
       /org/freedesktop/systemd1/unit/some_2eservice \
       org.freedesktop.systemd1.Service

The same information is available statically in the D-Bus introspection XML
files installed by the ``systemd-dev`` package under
``/usr/share/dbus-1/interfaces/``, one file per interface.  These files are
the authoritative source for property type signatures used in
``StartTransientUnit`` calls.

******************
External Resources
******************

- `D-Bus specification <https://dbus.freedesktop.org/doc/dbus-specification.html>`_
- `The new sd-bus API of systemd <https://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html>`_
- `org.freedesktop.systemd1 D-Bus interface <https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.systemd1.html>`_
- :linux:man5:`systemd.resource-control` — resource control properties including ``DeviceAllow``
