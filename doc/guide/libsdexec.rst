.. _libsdexec:

######################
Systemd Client Library
######################

.. default-domain:: c

libsdexec (``src/common/libsdexec/``) is a C library that abstracts the D-Bus
interface to systemd.  It translates libsubprocess-style command objects into
``StartTransientUnit`` D-Bus calls, manages I/O channels, and tracks unit
state.

Unit States
===========

Unit state is tracked with two enums that map to the ``ActiveState``
and ``SubState`` D-Bus properties on ``org.freedesktop.systemd1.Unit``.

.. c:enum:: sdexec_state_t

   .. c:enumerator:: STATE_UNKNOWN

      State string was not recognized.

   .. c:enumerator:: STATE_INACTIVE

      Unit is stopped (ActiveState = "inactive").

   .. c:enumerator:: STATE_ACTIVATING

      Unit is starting (ActiveState = "activating").

   .. c:enumerator:: STATE_ACTIVE

      Unit is running (ActiveState = "active").

   .. c:enumerator:: STATE_DEACTIVATING

      Unit is stopping (ActiveState = "deactivating").

   .. c:enumerator:: STATE_FAILED

      Unit failed to start or was killed (ActiveState = "failed").

.. c:enum:: sdexec_substate_t

   .. c:enumerator:: SUBSTATE_UNKNOWN

      Substate string was not recognized.

   .. c:enumerator:: SUBSTATE_DEAD

      No processes running.

   .. c:enumerator:: SUBSTATE_START

      Start sequence in progress.

   .. c:enumerator:: SUBSTATE_RUNNING

      Main process is running.

   .. c:enumerator:: SUBSTATE_EXITED

      Main process has exited; unit cleanup may be pending.

   .. c:enumerator:: SUBSTATE_FAILED

      Unit stopped due to an error.

.. c:function:: const char *sdexec_statetostr(sdexec_state_t state)
                const char *sdexec_substatetostr(sdexec_substate_t substate)
                sdexec_state_t sdexec_strtostate(const char *s)
                sdexec_substate_t sdexec_strtosubstate(const char *s)

   Convert between :c:enum:`sdexec_state_t` / :c:enum:`sdexec_substate_t`
   and their string representations.  Conversion from an unknown string
   returns the ``_UNKNOWN`` enumerator.

Unit Object
===========

A ``struct unit`` accumulates D-Bus property updates for a single transient
unit and exposes typed accessors.  It is the primary object passed between
the property and lifecycle layers.

.. c:function:: struct unit *sdexec_unit_create(const char *name)

   Allocate a unit object for the unit named *name*.  Returns NULL on
   allocation failure.

.. c:function:: void sdexec_unit_destroy(struct unit *unit)

   Free *unit* and all associated resources.

.. c:function:: bool sdexec_unit_update(struct unit *unit, json_t *property_dict)

   Apply a property dictionary (from :c:func:`sdexec_property_changed_dict`
   or :c:func:`sdexec_property_get_all_dict`) to *unit*.  Returns true
   if any tracked field changed, false if the update was a no-op.

.. c:function:: bool sdexec_unit_update_frominfo(struct unit *unit, struct unit_info *info)

   Like :c:func:`sdexec_unit_update` but applies data from a
   :c:struct:`unit_info` returned by :c:func:`sdexec_list_units_next`.

.. c:function:: void *sdexec_unit_aux_get(struct unit *unit, const char *name)
                int   sdexec_unit_aux_set(struct unit *unit, const char *name, void *aux, flux_free_f destroy)

   Attach or retrieve arbitrary caller data keyed by *name*.
   :c:func:`sdexec_unit_aux_set` returns 0 on success, -1 on error.

.. c:function:: sdexec_state_t    sdexec_unit_state(struct unit *unit)
                sdexec_substate_t sdexec_unit_substate(struct unit *unit)
                pid_t             sdexec_unit_pid(struct unit *unit)
                const char       *sdexec_unit_path(struct unit *unit)
                const char       *sdexec_unit_name(struct unit *unit)

   Accessors for current state, substate, main PID, D-Bus object path, and
   unit name.

.. c:function:: int sdexec_unit_wait_status(struct unit *unit)

   Returns the :linux:man2:`wait` status of the main process if
   :c:func:`sdexec_unit_has_finished` returns true, otherwise -1.

.. c:function:: int sdexec_unit_systemd_error(struct unit *unit)

   Returns the systemd error code if :c:func:`sdexec_unit_has_failed`
   returns true, otherwise -1.

.. c:function:: bool sdexec_unit_has_started(struct unit *unit)
                bool sdexec_unit_has_finished(struct unit *unit)
                bool sdexec_unit_has_failed(struct unit *unit)

   Lifecycle predicates.  ``has_started`` becomes true when the unit reaches
   ACTIVE/RUNNING and ``ExecMainPID`` is set.  ``has_finished`` becomes true
   when ``ExecMainCode`` is available.  ``has_failed`` becomes true when the
   unit reaches FAILED state.

Starting Units
==============

.. c:function:: flux_future_t *sdexec_start_transient_unit(flux_t *h, uint32_t rank, const char *mode, json_t *cmd, int stdin_fd, int stdout_fd, int stderr_fd, flux_error_t *error)

   Send a ``StartTransientUnit`` D-Bus call via ``sdbus.call`` on *rank*.
   *cmd* is a libsubprocess command object; the unit name must be set as the
   ``SDEXEC_NAME`` command option (with ``.service`` suffix).

   *stdin_fd*, *stdout_fd*, and *stderr_fd* are file descriptors to pass to
   the new unit as its stdio streams.  Pass -1 to have systemd manage a
   stream itself.  The caller may close its copies after the future is first
   fulfilled.

   Command options with the ``SDEXEC_PROP_`` prefix are translated to service
   unit properties.  The following are given special treatment; all others
   are passed as strings:

   .. list-table::
      :header-rows: 1

      * - Option (after SDEXEC_PROP\_)
        - D-Bus type
        - Value format
      * - MemoryHigh, MemoryMax, MemoryLow, MemoryMin
        - t (uint64)
        - "infinity", "98%", or quantity with K/M/G/T suffix
      * - AllowedCPUs
        - s (string)
        - Flux idset notation, e.g. "0,2-4,7"
      * - DeviceAllow
        - a(ss)
        - Comma-separated "specifier perms" pairs,
          e.g. "/dev/nvidiactl rw,/dev/nvidia0 rw"
      * - DevicePolicy
        - s (string)
        - "auto", "closed", or "strict"

   See :linux:man5:`systemd.resource-control` for property semantics.

   .. note::

      ``DeviceAllow`` and ``DevicePolicy`` are accepted by
      ``StartTransientUnit`` but are not enforced in the Flux user
      systemd instance.  Systemd implements device containment via eBPF
      ``BPF_CGROUP_DEVICE`` programs, which require ``CAP_BPF`` or
      ``CAP_SYS_ADMIN`` — capabilities the unprivileged Flux user
      instance does not hold and that cannot be delegated via cgroup
      ownership.  Per-job device containment will be implemented through
      the IMP until systemd makes device restriction delegatable without
      elevated privileges.

   Returns a :type:`flux_future_t`.  On error, returns NULL with
   *error* set if non-NULL.

.. c:function:: int sdexec_start_transient_unit_get(flux_future_t *f, const char **job)

   Extract the job object path from a fulfilled
   :c:func:`sdexec_start_transient_unit` future.  The path is valid for
   the lifetime of *f*.  Returns 0 on success, -1 with ``errno`` set on
   error.

Stopping Units
==============

.. c:function:: flux_future_t *sdexec_stop_unit(flux_t *h, uint32_t rank, const char *name, const char *mode)

   Send a ``StopUnit`` D-Bus call for the unit *name* on *rank*.  *mode* is
   typically ``"fail"``; see the `systemd D-Bus API
   <https://www.freedesktop.org/wiki/Software/systemd/dbus/>`_ for other
   values.  Returns a :type:`flux_future_t`.

.. c:function:: flux_future_t *sdexec_reset_failed_unit(flux_t *h, uint32_t rank, const char *name)

   Send a ``ResetFailedUnit`` D-Bus call to clear the failed state of unit
   *name* on *rank*.

.. c:function:: flux_future_t *sdexec_kill_unit(flux_t *h, uint32_t rank, const char *name, const char *who, int signum)

   Send a ``KillUnit`` D-Bus call to deliver *signum* to the processes in
   unit *name* on *rank*.  *who* selects the target: ``"main"`` for the main
   process, ``"control"`` for the control process, or ``"all"`` for both.

Properties
==========

.. c:function:: flux_future_t *sdexec_property_get(flux_t *h, const char *service, uint32_t rank, const char *path, const char *name)

   Issue a ``Get`` call on the D-Bus Properties interface at object path
   *path* via *service* (typically ``"sdbus"``).  Use
   :c:func:`sdexec_property_get_unpack` to extract the result.

.. c:function:: int sdexec_property_get_unpack(flux_future_t *f, const char *fmt, ...)

   Unpack the variant value from a fulfilled :c:func:`sdexec_property_get`
   future.  *fmt* is a Jansson-style unpack format string applied to the
   unwrapped value.  Returns 0 on success, -1 on error.

.. c:function:: flux_future_t *sdexec_property_get_all(flux_t *h, const char *service, uint32_t rank, const char *path)

   Issue a ``GetAll`` call on the D-Bus Properties interface at *path*.
   Use :c:func:`sdexec_property_get_all_dict` to access the result.

.. c:function:: json_t *sdexec_property_get_all_dict(flux_future_t *f)

   Return the property dictionary from a fulfilled
   :c:func:`sdexec_property_get_all` future.  The dict is valid for the
   lifetime of *f* and can be queried with :c:func:`sdexec_property_dict_unpack`
   or passed to :c:func:`sdexec_unit_update`.

.. c:function:: flux_future_t *sdexec_property_changed(flux_t *h, const char *service, uint32_t rank, const char *path)

   Subscribe to ``PropertiesChanged`` signals for the unit at *path*.  Each
   fulfillment of the returned streaming future represents one signal.  Pass
   *path* = NULL to receive signals for all paths and use
   :c:func:`sdexec_property_changed_path` to filter.

.. c:function:: json_t      *sdexec_property_changed_dict(flux_future_t *f)
                const char  *sdexec_property_changed_path(flux_future_t *f)

   Extract the property dictionary or object path from the current
   fulfillment of a :c:func:`sdexec_property_changed` future.  Both are
   valid for the lifetime of the current fulfillment.  The dict can be passed
   directly to :c:func:`sdexec_unit_update`.

.. c:function:: int sdexec_property_dict_unpack(json_t *dict, const char *name, const char *fmt, ...)

   Look up property *name* in a property dictionary and unpack its variant
   value using the Jansson format string *fmt*.  Returns 0 on success, -1
   if the property is absent or the type does not match.

I/O Channels
============

.. c:macro:: CHANNEL_LINEBUF

   Flag for :c:func:`sdexec_channel_create_output`: buffer output and deliver
   it line-by-line rather than in arbitrary chunks.

.. c:type:: void (*channel_output_f)(struct channel *ch, json_t *io, void *arg)

   Output callback type.  *io* is an ioencode-formatted JSON object
   containing the stream name, broker rank, and data (or EOF flag).

.. c:type:: void (*channel_error_f)(struct channel *ch, flux_error_t *error, void *arg)

   Error callback type.  Called on a read error before the output callback
   delivers EOF.

.. c:function:: struct channel *sdexec_channel_create_output(flux_t *h, const char *name, size_t bufsize, int flags, channel_output_f output_cb, channel_error_f error_cb, void *arg)

   Create a channel for output from the systemd unit (stdout or stderr).
   The internal fd watcher is not started until
   :c:func:`sdexec_channel_start_output` is called.  Set
   :c:macro:`CHANNEL_LINEBUF` in *flags* to enable line buffering.

.. c:function:: struct channel *sdexec_channel_create_input(flux_t *h, const char *name)

   Create a channel for input to the systemd unit (stdin).  Write to it
   using :c:func:`sdexec_channel_write`.

.. c:function:: int sdexec_channel_write(struct channel *ch, json_t *io)

   Write ioencode-formatted data to an input channel.  The rank and
   stream name fields of *io* are ignored.  This may block if the socket
   buffer is full.  Returns 0 on success, -1 on error.

.. c:function:: int         sdexec_channel_get_fd(struct channel *ch)
                const char *sdexec_channel_get_name(struct channel *ch)

   Return the file descriptor for the systemd end of the socketpair, or the
   channel name.  :c:func:`sdexec_channel_get_fd` returns -1 if the fd
   has been closed or *ch* is NULL.

.. c:function:: void sdexec_channel_close_fd(struct channel *ch)

   Close the systemd-side fd.  Call this after systemd has duped it — the
   response handler for ``StartTransientUnit`` is the right place.

.. c:function:: void sdexec_channel_start_output(struct channel *ch)

   Arm the reactor watcher for an output channel.  Data arriving after this
   call triggers *output_cb*.

.. c:function:: void sdexec_channel_destroy(struct channel *ch)

   Destroy *ch* and free all associated resources.

Unit List
=========

.. c:struct:: unit_info

   Snapshot of a unit entry returned by ``ListUnitsByPatterns``.

   .. c:member:: const char *name

      Unit name (e.g. ``flux-job-123-abc.service``).

   .. c:member:: const char *description

      Unit description string.

   .. c:member:: const char *load_state

      Load state ("loaded", "not-found", etc.).

   .. c:member:: const char *active_state

      Active state string; see :c:enum:`sdexec_state_t`.

   .. c:member:: const char *sub_state

      Sub-state string; see :c:enum:`sdexec_substate_t`.

   .. c:member:: const char *path

      D-Bus object path for the unit.

   .. c:member:: json_int_t job_id

      ID of a queued systemd job for this unit, or 0 if none.

   .. c:member:: const char *job_type

      Type string of the queued job ("start", "stop", etc.).

   .. c:member:: const char *job_path

      D-Bus object path of the queued job.

.. c:function:: flux_future_t *sdexec_list_units(flux_t *h, const char *service, uint32_t rank, const char *pattern)

   Issue a ``ListUnitsByPatterns`` D-Bus call via *service* on *rank*.
   *pattern* is a glob matched against unit names; pass ``"*"`` for all units.
   Iterate results with :c:func:`sdexec_list_units_next`.

.. c:function:: bool sdexec_list_units_next(flux_future_t *f, struct unit_info *info)

   Fill *info* with the next unit entry from a fulfilled
   :c:func:`sdexec_list_units` future.  Returns true on success,
   false when the list is exhausted.  Pointers in *info* are valid
   until the next call or until *f* is destroyed.

************
Object Paths
************

systemd maps unit names to D-Bus object paths by appending an encoded form of
the unit name to ``/org/freedesktop/systemd1/unit/``.  Any character that is
not alphanumeric or underscore is replaced with ``_XX`` where XX is the
lowercase hex byte value.  libsdexec handles this via :linux:man3:`sd_bus_path_encode` and
:linux:man3:`sd_bus_path_decode` in ``objpath.c``.

.. note::

   This encoding is a systemd convention, not a D-Bus standard.  Its presence
   in sdbus's object-path type handler means sdbus has inadvertently absorbed
   systemd-specific knowledge that ideally would live only here.

.. list-table::
   :header-rows: 1

   * - Unit name
     - D-Bus object path
   * - dbus.service
     - /org/freedesktop/systemd1/unit/dbus_2eservice
   * - flux-broker.service
     - /org/freedesktop/systemd1/unit/flux_2dbroker_2eservice
   * - flux-job-f23abc.service
     - /org/freedesktop/systemd1/unit/flux_2djob_2df23abc_2eservice
   * - user\@1000.service
     - /org/freedesktop/systemd1/unit/user_401000_2eservice

*************************
D-Bus Interface Reference
*************************

The following D-Bus interfaces and members are used by the systemd integration.
See the `org.freedesktop.systemd1 man page
<https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.systemd1.html>`_
for the full interface specification.

org.freedesktop.systemd1.Manager
=================================

.. list-table::
   :header-rows: 1

   * - Method
     - In
     - Out
     - Notes
   * - StartTransientUnit
     - ssa(sv)a(sa(sv))
     - o
     - Launches a transient unit; returns the job object path
   * - StopUnit
     - ss
     - o
     - Stops a unit; mode is typically "fail"
   * - KillUnit
     - ssi
     -
     - Sends a signal to unit processes; *who* is "main", "control", or "all"
   * - ResetFailedUnit
     - s
     -
     - Clears a failed unit so it can be restarted
   * - ListUnitsByPatterns
     - asas
     - a(ssssssouso)
     - Lists units matching state/name patterns (used in tests)

org.freedesktop.systemd1.Service
=================================

Properties polled via GetAll or received via PropertiesChanged:

.. list-table::
   :header-rows: 1

   * - Property
     - D-Bus type
     - Notes
   * - ExecMainPID
     - u
     - Main process PID; available once unit is active
   * - ExecMainCode
     - i
     - Exit code (CLD_* constant)
   * - ExecMainStatus
     - i
     - Raw wait status
   * - ActiveState
     - s
     - One of: inactive, activating, active, deactivating, failed
   * - SubState
     - s
     - One of: dead, start, running, exited, failed
   * - Result
     - s
     - One of: done, exited, killed, timeout, etc.

org.freedesktop.DBus.Properties
================================

.. list-table::
   :header-rows: 1

   * - Method / Signal
     - In
     - Out
     - Notes
   * - Get
     - ss
     - v
     - Fetch a single property by interface and name
   * - GetAll
     - s
     - a{sv}
     - Fetch all properties as a string→variant dictionary
   * - PropertiesChanged (signal)
     -
     -
     - Emitted on property changes; sdexec subscribes per-unit

org.freedesktop.DBus
=====================

.. list-table::
   :header-rows: 1

   * - Method
     - Notes
   * - Subscribe
     - Request signal delivery to this connection
   * - AddMatch
     - Add a match rule to filter incoming signals

******************
External Resources
******************

- :linux:man5:`systemd.resource-control`
- :linux:man5:`systemd.service`
