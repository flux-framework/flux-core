*************************
Day to day administration
*************************

Starting Flux
=============

Systemd may be configured to start Flux automatically at boot time,
as long as the network that carries its overlay network will be
available at that time.  Alternatively, Flux may be started manually, e.g.

.. code-block:: console

 $ sudo pdsh -w fluke[3,108,6-103] sudo systemctl start flux

Flux brokers may be started in any order, but they won't come online
until their parent in the tree based overlay network is available.

If Flux was not shut down properly, for example if the rank 0 broker
crashed or was killed, then Flux starts in a safe mode with job submission
and scheduling disabled.  :man1:`flux-uptime` shows the general state
of Flux, and :man1:`flux-startlog` prints a record of Flux starts and
stops, including any crashes.

Stopping Flux
=============

The full Flux system instance may be temporarily stopped by running
the following on the rank 0 node:

.. code-block:: console

 $ sudo flux shutdown

This kills any running jobs, but preserves job history and the queue of
jobs that have been submitted but have not yet allocated resources.
This state is held in the ``content.sqlite`` that was configured above.
See also :man1:`flux-shutdown`.

.. note::
    ``flux-shutdown --gc`` should be used from time to time to perform offline
    KVS garbage collection.  This, in conjunction with configuring inactive
    job purging, keeps the size of the ``content.sqlite`` database in check
    and improves Flux startup time.

The brokers on other nodes will automatically shut down in response,
then respawn, awaiting the return of the rank 0 broker.

To shut down a single node running Flux, simply run

.. code-block:: console

 $ sudo systemctl stop flux

on that node.

Configuration update
====================

After changing flux broker or module specific configuration in the TOML
files under ``/etc/flux``, the configuration may be reloaded with

.. code-block:: console

 $ sudo systemctl reload flux

on each rank where the configuration needs to be updated. The broker will
reread all configuration files and notify modules that configuration has
been updated.

Configuration which applies to the ``flux-imp`` or job shell will be reread
at the time of the next job execution, since these components are executed
at job launch.

.. warning::
    Many configuration changes have no effect until the Flux broker restarts.
    This should be assumed unless otherwise noted.  See :man5:`flux-config`
    for more information.

Viewing resource status
=======================

Flux offers two different utilities to query the current resource state.

``flux resource status`` is an administrative command which lists ranks
which are available, online, offline, excluded, or drained along with
their corresponding node names. By default, sets which have 0 members
are not displayed, e.g.

.. code-block:: console

 $ flux resource status
      STATE UP NNODES NODELIST
      avail  ✔     78 fluke[6-16,19-23,25-60,62-63,68,71-73,77-78,80,82-86,88,90-91,93,95-101,103]
     avail*  ✗      6 fluke[17,24,61,79,92,102]
    exclude  ✔      3 fluke[1,3,108]
    drained  ✔     13 fluke[18,64-65,67,69-70,74-76,81,87,89,94]
   drained*  ✗      1 fluke66

To list a set of states explicitly, use the ``--states`` option:
(Run ``--states=help`` to get a list of valid states)

.. code-block:: console

 $ flux resource status --states=drained,exclude
     STATE UP NNODES NODELIST
   exclude  ✔      3 fluke[1,3,108]
   drained  ✔     13 fluke[18,64-65,67,69-70,74-76,81,87,89,94]
  drained*  ✗      1 fluke66

This option is useful to get a list of ranks or hostnames in a given
state. For example, the following command fetches the hostlist
for all resources configured in a Flux instance:

.. code-block:: console

 $ flux resource status -s all -no {nodelist}
 fluke[1,3,6-103,108]

In contrast to ``flux resource status``, the ``flux resource list``
command lists the *scheduler*'s view of available resources. This
command shows the free, allocated, and unavailable (down) resources,
and includes nodes, cores, and gpus at this time:

.. code-block:: console

 $ flux resource list
     STATE QUEUE      PROPERTIES NNODES   NCORES NODELIST
      free batch                     71      284 fluke[6-16,19-23,25-60,62-63,68,71-73,77-78,80,82-86,88,90-91,93,95]
      free debug                      6       24 fluke[96-101]
      free debug      testprop        1        4 fluke103
 allocated                            0        0 
      down batch                     19       76 fluke[17-18,24,61,64-67,69-70,74-76,79,81,87,89,92,94]
      down debug      testprop        1        4 fluke102

With ``--o rlist``, ``flux resource list`` will show a finer grained list
of resources in each state, instead of a nodelist:

.. code-block:: console

 $ flux resource list -o rlist
     STATE QUEUE    PROPERTIES NNODES   NCORES    NGPUS LIST
      free batch                   71      284        0 rank[3-13,16-20,22-57,59-60,65,68-70,74-75,77,79-83,85,87-88,90,92]/core[0-3]
      free debug                    6       24        0 rank[93-98]/core[0-3]
      free debug    testprop        1        4        0 rank100/core[0-3]
 allocated                          0        0        0
      down batch                   19       76        0 rank[14-15,21,58,61-64,66-67,71-73,76,78,84,86,89,91]/core[0-3]
      down debug    testprop        1        4        0 rank99/core[0-3]


Draining resources
==================

Resources may be temporarily removed from scheduling via the
``flux resource drain`` command. Currently, resources may only be drained
at the granularity of a node, represented by its hostname or broker rank,
for example:

.. code-block:: console

 $ sudo flux resource drain fluke7 node is fubar
 $ sudo flux resource drain
 TIMESTAMP            STATE    RANK     REASON                         NODELIST
 2020-12-16T09:00:25  draining 2        node is fubar                  fluke7

Any work running on the "draining" node is allowed to complete normally.
Once there is nothing running on the node its state changes to "drained":

.. code-block:: console

 $ sudo flux resource drain
 TIMESTAMP            STATE    RANK     REASON                         NODELIST
 2020-12-16T09:00:25  drained  2        node is fubar                  fluke7

To return drained resources use ``flux resource undrain``:

.. code-block:: console

 $ sudo flux resource undrain fluke7
 $ sudo flux resource drain
 TIMESTAMP            STATE    RANK     REASON                         NODELIST


Managing the Flux queue
=======================

The queue of jobs is managed by the flux job-manager, which in turn
makes allocation requests for jobs in priority order to the scheduler.
This queue can be managed using the ``flux-queue`` command.

.. code-block:: console

 Usage: flux-queue [OPTIONS] COMMAND ARGS
   -h, --help             Display this message.

 Common commands from flux-queue:
    enable          Enable job submission
    disable         Disable job submission
    start           Start scheduling
    stop            Stop scheduling
    status          Get queue status
    drain           Wait for queue to become empty.
    idle            Wait for queue to become idle.

The queue may be listed with the :man1:`flux-jobs` command.

Disabling job submission
------------------------

By default, the queue is *enabled*, meaning that jobs can be submitted
into the system. To disable job submission, e..g to prepare the system
for a shutdown, use ``flux queue disable``. To restore queue access
use ``flux queue enable``.

Stopping resource allocation
----------------------------

The queue may also be stopped with ``flux queue stop``, which disables
further allocation requests from the job-manager to the scheduler. This
allows jobs to be submitted, but stops new jobs from being scheduled.
To restore scheduling use ``flux queue start``.

Flux queue idle and drain
-------------------------

The ``flux queue drain`` and ``flux queue idle`` commands can be used
to wait for the queue to enter a given state. This may be useful when
preparing the system for a downtime.

The queue is considered *drained* when there are no more active jobs.
That is, all jobs have completed and there are no pending jobs.
``flux queue drain`` is most useful when the queue is *disabled* .

The queue is "idle" when there are no jobs in the RUN or CLEANUP state.
In the *idle* state, jobs may still be pending. ``flux queue idle``
is most useful when the queue is *stopped*.

To query the current status of the queue use the ``flux queue status``
command:

.. code-block:: console

 $ flux queue status -v
 batch: Job submission is enabled
 batch: Scheduling is started
 debug: Job submission is enabled
 debug: Scheduling is started
 0 alloc requests queued
 0 alloc requests pending to scheduler
 0 free requests pending to scheduler
 0 running jobs

Managing Flux jobs
==================

Expediting/Holding jobs
-----------------------

To expedite or hold a job, set its *urgency* to the special values
EXPEDITE or HOLD.

.. code-block:: console

 $ flux job urgency ƒAiVi2Sj EXPEDITE

.. code-block:: console

 $ flux job urgency ƒAiVi2Sj HOLD

Canceling jobs
--------------

An active job may be canceled via the ``flux cancel`` command. An
instance owner may cancel any job, while a guest may only cancel their
own jobs.

All active jobs may be canceled with ``flux cancel --user=all``.

.. code-block:: console

 $ flux cancel --user=all --dry-run
 flux-cancel: Would cancel 3 jobs
 $ flux cancel --user=all
 flux-cancel: Canceled 3 jobs (0 errors)

The set of jobs matched by the ``cancel`` command may also be restricted
via the ``-s, --states=STATES`` and ``-u, --user=USER`` options.

Software update
===============

Flux will eventually support rolling software upgrades, but prior to
major release 1, Flux software release versions should not be assumed
to inter-operate.  Furthermore, at this early stage, Flux software
components (e.g. ``flux-core``, ``flux-sched``, ``flux-security``,
and ``flux-accounting``)  should only only be installed in recommended
combinations.

.. note::
    Mismatched broker versions are detected as brokers attempt to join
    the instance.  The version is currently required to match exactly.

.. warning::
    Ensure that flux is completely stopped before beginning a software
    update.  If this is not observed, Flux may fail to shut down cleanly.
