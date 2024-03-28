================
flux-shutdown(1)
================


SYNOPSIS
========

**flux** **shutdown** [*OPTIONS*] [*TARGET*]


DESCRIPTION
===========

.. program:: flux shutdown

The :program:`flux shutdown` command causes the default Flux instance, or the
instance specified by *TARGET*, to exit RUN state and begin the process
of shutting down.  *TARGET* may be either a native Flux URI or a high level
URI, as described in :man1:`flux-uri`.

Only the rank 0 broker in RUN state may be targeted for shutdown.
The current broker state may be viewed with :man1:`flux-uptime`.

If the instance is running an initial program, that program is terminated
with SIGHUP.  Note that the broker exit value normally reflects the
exit code of the initial program, so if it is terminated by this signal,
the broker exits with 128 + 1 = 129.

If the broker was launched by systemd, an exit code is used that informs
systemd not to restart the broker.

Broker log messages that are posted during shutdown are displayed by
the shutdown command on stderr, until the broker completes executing its
``rc3`` script.  By default, log messages with severity level <= LOG_INFO
are printed.

A Flux system instance requires offline KVS garbage collection to remove
deleted KVS content and purged job directories, which accrue over time and
increase storage overhead and restart time.  It is recommended that the
:option:`--gc` option be used on a routine basis to optimize Flux.


OPTIONS
=======

:program:`flux shutdown` accepts the following options:

.. option:: -h, --help

   Display options and exit

.. option:: --background

   Start the shutdown and exit immediately, without monitoring the process
   and displaying log messages.

.. option:: --quiet

   Show only error log messages (severity level <= LOG_WARNING level).

.. option:: --verbose=[LEVEL]

   Increase output verbosity.  Level 1 shows all log messages.  Higher
   verbosity levels are reserved for future use.

.. option:: --dump=PATH

   Dump a checkpoint of KVS content to *PATH* using :man1:`flux-dump` after the
   KVS has been unloaded.  The dump may be restored into a new Flux instance
   using :man1:`flux-restore`.  Dump creation adds time to the shutdown
   sequence, proportional to the amount of data in the KVS.
   :option:`--dump=auto` is a special case equivalent to :option:`--gc`.

.. option:: --gc

   Prepare for offline KVS garbage collection by dumping a checkpoint of KVS
   content to ``dump/<date>.tgz`` in *statedir*, if defined, otherwise in
   the broker's current working directory.  Create a symbolic link named
   ``dump/RESTORE`` pointing to the dump file.  When this link is discovered
   on instance startup, the content database is truncated and recreated from
   the dump, and the link is removed.  :linux:man8:`systemd-tmpfiles`
   automatically cleans up dump files in ``/var/lib/flux/dump`` after 30 days.

.. option:: --skip-gc

   When garbage collection has been enabled automatically, as indicated
   by the ``content.dump`` broker attribute, this option disables it
   during shutdown.  Otherwise it is a preemptive "no" answer to the garbage
   collection prompt.

.. option:: -y, --yes

   Answer yes to any yes/no questions.

.. option:: -n, --no

   Answer no to any yes/no questions.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-start`, :man1:`flux-uptime`, :man1:`flux-uri`, :man1:`flux-dump`,
:man5:`flux-config-kvs`,:linux:man8:`systemd-tmpfiles`
