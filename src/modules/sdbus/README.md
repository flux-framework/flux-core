## flux systemd dbus bridge

The Flux `sdbus` module connects to `$DBUS_SESSION_BUS_ADDRESS`, which the
Flux systemd unit file sets to the address of the user level systemd instance
of the `flux` user.  It then makes D-Bus method calls on behalf of Flux users
that communicate with `sdbus` using Flux RPCs.  It also allows D-Bus signals
to be subscribed to via a Flux streaming RPC.

Useful references:
- [D-Bus specification](https://dbus.freedesktop.org/doc/dbus-specification.html)
- [The new sd-bus API of systemd](https://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html)

### Moving messages

`sdbus` communicates with D-Bus at a fairly low level so it can operate
reactively, that is, without busy-polling or blocking.  The main libsystemd
interfaces used for this are:

- [sd_bus_open_user(3)](https://man7.org/linux/man-pages/man3/sd_bus_open_user.3.html), [sd_bus_close(3)](https://man7.org/linux/man-pages/man3/sd_bus_close.3.html) and friends.
- [sd_bus_process(3)](https://man7.org/linux/man-pages/man3/sd_bus_process.3.html), [sd_bus_get_fd(3)](https://man7.org/linux/man-pages/man3/sd_bus_get_fd.3.html) and friends
- [sd_bus_message_send(3)](https://man7.org/linux/man-pages/man3/sd_bus_message_send.3.html)

The Flux `sdbus` module takes care of matching method-reply and method-error
messages to method-call messages.  The higher level libsystemd functions that
handle that in the systemd code are not used.

### Message translation

Flux message payloads in JSON are translated to and from D-Bus messages.
Currently `sdbus` translates a subset of possible D-Bus messages, however
it does so in a way that leaves the door open for a future translator that
can handle any message and uses D-Bus introspection to obtain message
signatures on the fly.

Supported (interface, member) tuples, and their signatures are listed in
interface.c.  The low-level message translation occurs in message.c.
Both may be amended as needed to support new method calls.

### Systemd

The main use case of `sdbus` is communicating with systemd.  Some useful
references are:

- [The D-Bus API of systemd/PID 1](https://www.freedesktop.org/wiki/Software/systemd/dbus/)
- [systemd.unit(5)](https://man7.org/linux/man-pages/man5/systemd.unit.5.html)
