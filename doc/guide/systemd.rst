.. _systemd:

##################
Systemd Execution
##################

Flux can optionally execute jobs as transient systemd units, providing
cgroup-based resource enforcement and process isolation.  This document
describes the internal architecture of that integration for developers.

For administrator configuration, see :man5:`flux-config-systemd` and
:man5:`flux-config-exec`.

The systemd integration consists of three components:

**libsdexec**
  A common C library that abstracts the D-Bus interface to systemd.  It
  translates libsubprocess-style command objects into ``StartTransientUnit``
  D-Bus calls, manages I/O channels, and tracks unit state.

**sdbus module**
  A broker module that maintains a connection to the user-level systemd
  D-Bus session.  It translates incoming Flux RPCs to D-Bus method calls and
  translates D-Bus signals into Flux streaming RPC responses.  One instance
  runs per broker rank.

**sdexec module**
  A broker module that implements the ``sdexec`` subprocess service, an
  alternative to the built-in ``rexec`` service.  It uses libsdexec and
  sdbus to launch jobs as transient systemd units, multiplex their I/O, and
  track their lifecycle.  One instance runs per broker rank.

When ``job-exec`` selects the ``sdexec`` service, it issues ``sdexec.exec``
RPCs to the sdexec module on each rank of the allocation.  sdexec then drives
the full unit lifecycle via sdbus and libsdexec, returning streaming responses
(started, output, finished) that mirror the libsubprocess client protocol.

.. toctree::
   :maxdepth: 1

   sdbus
   sdexec
   libsdexec
