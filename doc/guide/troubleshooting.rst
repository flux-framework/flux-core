.. _troubleshooting:

#####################
Troubleshooting Guide
#####################

This guide gives a quick overview of commands and strategies which might
be useful when troubleshooting Flux jobs. It is organized by stage in the job
lifecycle.

.. toctree

**************
Job Submission
**************

If jobs cannot be submitted to Flux, check the following:

Verify the target queue or queues are enabled with :command:`flux queue status`
or :command:`flux queue list`:

.. code-block:: console

  $ flux  queue status pdebug
  pdebug: Job submission is enabled
  pdebug: Scheduling is started

Check the job-ingest module stats to ensure the module is loaded and
that there is no backlog of requests for the job frobnicator or validator
pipelines, if configured. The job-ingest module is loaded on every node,
so an issue occurring on just one node may indicate an ingest issue:

.. code-block:: console

  # flux module stats job-ingest | jq .pipeline
  {
    "frobnicator": {
      "running": 1,
      "requests": 2357,
      "errors": 3,
      "trash": 0,
      "backlog": 0,
      "pids": [
        3280386,
        0,
        0,
        0
      ]
    },
    "validator": {
      "running": 1,
      "requests": 2354,
      "errors": 5,
      "trash": 0,
      "backlog": 0,
      "pids": [
        3280387,
        0,
        0,
        0
      ]
    }
  }

If there is a large ``errors`` count, check :command:`flux dmesg -H` (or
:command:`journalctl -u flux` in a system instance) for errors.

If there is a nonzero ``backlog`` count, check for stuck frobnicator or
validator processes. These will be children of the :command:`flux-broker`,
and can be found via :linux:man1:`pstree`. For example, for a system instance:

.. code-block:: console

 # pstree -Tplu flux
 flux-broker-114(42368)─┬─python3(3282327)
                        └─python3(3282328)

If validator or frobnicator processes appear to be stuck, then they may
need to manually be killed (after collecting any possible debug).

If job submission is failing after ingest, a jobtap plugin may be involved.
Jobtap plugins may further validate a job after it is processed by job-ingest.

Loaded jobtap plugins are listed with :command:`flux jobtap list`. To
include builtin plugins use :command:`flux jobtap list -a`. Some jobtap
plugins support a query with :command:`flux jobtap query <name>.so`. Plugins
may be temporarily removed with :command:`flux jobtap remove <name>.so`
if they are suspect.

****************
Job Dependencies
****************

After a job is ingested, a :option:`validate` event transitions
the job to the :option:`DEPEND` state. If the job remains in the
:option:`DEPEND` state, then an unsatisfied dependency has been placed on
the job. Outstanding dependencies are typically available in the output
of :man1:`flux-jobs`. More information may be obtained by examining the
eventlog for :option:`dependency-add` events, and/or querying the jobtap
plugin responsible for the dependency, e.g.

.. code-block:: console

 # flux jobtap query .dependency-after | jq .dependencies[0]
 {
   "id": 328688799268861952,
   "depid": 328688808429221888,
   "type": "after-finish",
   "description": "after-finish=fmFckQ8UWjZ"
 }

Dependencies may be added to job for numerous reasons, including holding
the job in the depend state until some setup is complete. Consult with the
specific component that placed the dependency for further details.

If an expected dependency is not added to a job, then ensure the associated
plugin has not been removed. For example, if a job submitted with
:command:`flux submit -N1 --begin-time=+1h command` does not stay in
the :option:`DEPEND` state for 1 hour, then ensure the
:option:`.begin-time` builtin jobtap plugin is loaded:

.. code-block:: console

  # flux jobtap list -a | grep begin-time

.. note::

  Without :option:`-a`, :command:`flux jobtap list` suppresses the output
  of builtin plugins, which always start with a single ``.``.

If the plugin has somehow been removed. Try reloading it:

.. code-block:: console

  # flux jobtap load .begin-time
  # flux jobtap list -a | grep begin-time
  .begin-time


******************
Job Prioritization
******************

After all dependencies are resolved and a :option:`depend` event is emitted,
the job transitions to the :option:`PRIORITY` state. In this state, the job
is assigned an initial priority.

If jobs are stuck in the :option:`PRIORITY` state then the currently loaded
priority plugin may not be able to assign a priority. Check with the provider
of the priority plugin for more details.


**************
Job Scheduling
**************

Once a job receives an initial priority it transitions to the :option:`SCHED`
state. In this state the job manager sends an allocation request to the
scheduler, which will reply when the job has been assigned resources. The
number of outstanding alloc requests can be viewed with
:command:`flux queue status -v`:

.. code-block:: console

  $ flux queue status -v
  [snip]
  0 alloc requests queued
  88 alloc requests pending to scheduler
  181 running jobs

If jobs are stuck in the :option:`SCHED` state, obvious things to check are

 * A scheduler is loaded :command:`flux module list | grep sched`
 * The associated queue is not stopped: :command:`flux queue list` or
   :command:`flux queue status QUEUE`

It can be challenging to determine why a particular job is not being scheduled
if queues are started and the scheduler is loaded. Some things to check include:

 * Does the job have a specific constraint for resources that are not
   currently unavailable?

   .. code-block:: console

      # flux job info JOBID jobspec | jq .attributes.system.constraints
      {
        "and": [
          {
            "hostlist": [
              "host1071"
            ]
          },
          {
            "properties": [
              "pbatch"
            ]
          }
        ]
      }

   If host1071 is down, then this job can't currently be scheduled.

 * Is the job held or have a low priority?

  .. code-block:: console

    $ flux jobs -o {priority} fmctRr2YQ8f
    PRI
    0

If these do not yield any information, it may be useful to consult the
troubleshooting guide of the current scheduler module.

**********
Job Prolog
**********

After the scheduler responds to an alloc request, an :option:`alloc` event
is posted to the job eventlog:

.. code-block:: console

  # flux job eventlog -H fmctRr2YQ8f | grep alloc
  [  +0.122636] alloc

At this time, one or more :option:`prolog-start` events may be posted to
the eventlog by jobtap plugins. These events prevent the start request
from the job manager to the job execution system until a corresponding
:option:`prolog-finish` event is emitted.

Once all :option:`prolog-finish` events have been posted, the start request
is sent and a :option:`start` event is posted to the job eventlog when
the job execution system has launched all the job shells for the job.

.. code-block:: console

  # flux job eventlog -H fmctRr2YQ8f
  [snip]
  [  +0.122930] prolog-start description="job-manager.prolog"
  [  +4.288898] prolog-finish description="job-manager.prolog" status=0
  [  +4.328720] start

The :option:`job-manager.prolog` is managed by the :command:`perilog.so`
jobtap plugin, and is responsible for invoking the per-node job prolog
when configured (see :man5:`flux-config-job-manager`). Other plugins may
post :option:`prolog-start` events to prevent the job from starting while
they perform some kind of job prolog action.

If the :option:`prolog-finish` event for the :option:`job-manager.prolog`
is not posted in a timely manner, debug information can be obtained
directly from the :command:`perilog.so` plugin:

.. code-block:: console

  # flux jobtap query perilog.so | jq .procs
  {
    "ƒ22EmWZk1mkb": {
      "name": "prolog",
      "state": "running",
      "total": 4,
      "active": 1,
      "active_ranks": "0",
      "remaining_time": 55
    }
  }

The above shows that a prolog for job :option:`ƒ22EmWZk1mkb` is currently
running. It was executed on a :option:`total` of 4 broker ranks, and still
currently :option:`active` on 1 rank, rank 0. The prolog will time out in
55 seconds.

In this case there may be an issue on rank 0, since the prolog completed
on the other involved ranks already. Log into the host associated with broker
rank 0 and list the process tree of the :option:`flux-prolog@JOBID` unit
to see the process tree of the prolog:

.. code-block:: console

  # flux overlay lookup 0
  host1
  # ssh host1
  # systemd-cgls -u flux-prolog@f22EqDxY5rrK.service
  Unit flux-prolog@f22EqDxY5rrK.service
  ├─2056298 /bin/sh /etc/flux/system/prolog
  ├─2056301 /bin/sh /etc/flux/system/prolog.d/doit.sh
  └─2056302 hang

Since the prolog and epilog are executed as ephemeral systemd units,
the output from these scripts can be obtained from
:command:`journalctl -u flux-prolog@*`.

*************
Job Execution
*************

Once all :option:`prolog-start` events have a corresponding
:option:`prolog-finish` event, the job manager sends a start request to
the job execution system. The :option:`job-exec` module launches job
shells (via the IMP on a multi-user system). Once all shells have started
a :option:`start` event is posted to the main job eventlog.

When the start event is delayed, the :option:`job-exec` module can
be queried for the job to get some detail:

.. code-block:: console

  # flux module stats job-exec | jq .jobs.fmenpX365oV
  {
    "implementation": "bulk-exec",
    "ns": "job-331656486798360576",
    "critical_ranks": "0-3",
    "multiuser": 1,
    "has_namespace": 1,
    "exception_in_progress": 0,
    "started": 1,
    "running": 0,
    "finalizing": 0,
    "kill_timeout": 5.0,
    "kill_count": 0,
    "kill_shell_count": 0,
    "total_shells": 4,
    "active_shells": 3,
    "active_ranks": "8-10"
  }

This output shows that there should be a total of 4 job
shells (:option:`total_shells`), of which only 3 are active
(:option:`active_shells`) on ranks 8, 9, and 10 (:option:`active_ranks`). In
this case, the missing rank should be investigated (check output of
:command:`flux jobs -no {ranks} JOBID` for expected ranks). Note also that the
job-exec module has marked the job shells as :option:`started` but not yet
:option:`running`. This situation is highly unlikely but demonstrative. The
use of :option:`active_ranks` will be much more useful when jobs are stuck
exiting in :option:`CLEANUP` state due to a stuck job shell.

The exec eventlog may also be useful when jobs appear to be stuck at
launch:

.. code-block:: console

  # flux job eventlog -Hp exec fmenpX365oV
  [Apr02 18:48] init
  [  +0.021699] starting
  [  +0.346878] shell.init service="28220-shell-fmenpX365oV" leader-rank=1046 size=1
  [  +0.405225] shell.start taskmap={"version":1,"map":[[0,1,1,1]]}

The above output shows a normal exec eventlog. The job exec module writes
the :option:`init` and :option:`starting` events to the eventlog. The job
shell writes the :option:`shell.init` event after the first shell barrier
has been reached. The :option:`shell.start` event indicates all job shells
have started all job tasks.

If the :option:`shell.init` event is not posted, then one or more shells may
be slow to start or otherwise are not reaching the first barrier. Eventually,
the job execution system will time out the barrier and drain the affected
nodes with a message::

  job JOBID start timeout: possible node hang

The job will have a fatal exception raised of the form::

  start barrier timeout waiting for N/M nodes (ranks X-Y)

Check the affected ranks for issues.

***********
Job Cleanup
***********

When a job appears to be stuck in :option:`CLEANUP` state, first check
for the :option:`finish` event in the job eventlog:

.. code-block:: console

  # flux job eventlog -H JOBID | grep finish

If there is no finish event, then the job-exec module thinks there are
still active job shells. Use :command:`flux module stats job-exec` to find
the :option:`active_ranks`:

.. code-block:: console

  # flux module stats job-exec | jq .jobs.<JOBID>
  {
    "implementation": "bulk-exec",
    "ns": "job-331656486798360576",
    "critical_ranks": "0-3",
    "multiuser": 1,
    "has_namespace": 1,
    "exception_in_progress": 1,
    "started": 1,
    "running": 1,
    "finalizing": 0,
    "kill_timeout": 5.0,
    "kill_count": 1,
    "kill_shell_count": 0,
    "total_shells": 4,
    "active_shells": 1,
    "active_ranks": "8"
  }

In the output above, there is 1 (:option:`active_shells`) out of 4
(:option:`total_shells`) job shells still active. The shell is active on
rank 8 (:option:`active_ranks`). The hostname for rank 8 may be obtained
via :command:`flux overlay lookup 8`.

If there is a :option:`finish` event posted to the job eventlog,
then look for any :option:`epilog-start` event without a corresponding
:option:`epilog-finish`. Consult documentation of the affected epilog
action for further debugging. For a :option:`job-manager.epilog`,
use :option:`flux jobtap query perilog.so` to determine the state
of the epilog. Check for any unexpected active processes in the
:option:`active_ranks` key.


************
Housekeeping
************

After a job posts the :option:`clean` event to the job eventlog, resources
are released to the job manager, which then starts housekeeping on those
resources if configured.

Nodes in housekeeping are displayed in the output of
:command:`flux resource status`

.. code-block:: console

  $ flux resource status -s housekeeping
         STATE UP NNODES NODELIST
  housekeeping  ✔      8 tuolumne[1174,1747-1748,1751-1753,1775-1776]

More information is available with :command:`flux housekeeping list`

.. code-block:: console

   $ flux housekeeping list
         JOBID NNODES #ACTIVE  RUNTIME NODELIST
   fmkHggkEBMy      4       1   53.92s tuolumne[1174,1747-1748,1751]
   fmkHiMPjMsm      4       4   16.14s tuolumne[1752-1753,1775-1776]

See :man1:`flux-housekeeping` for details.

Like the prolog and epilog, housekeeping is executed in a systemd transient
unit, per-job, of the form :option:`flux-housekeeping@JOBID`. Use
:command:`systemd-cgls` to list processes and :command:`journalctl -u
flux-housekeeping@JOBID` to debug housekeeping scripts.

