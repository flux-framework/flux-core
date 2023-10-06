Interacting with Flux
=====================

.. _command_summary:

Command Summary
---------------

Here is an abbreviated list of Flux commands, to get you started while exploring
Flux.  If commands fail to connect to a Flux instance, refer to
:ref:`connect_to_flux` below.

To learn more, help is available within most commands, and many have
:ref:`Manual Pages <man-commands>`.

.. list-table::
   :header-rows: 1

   * - Example
     - Description

   * - ``flux help``

       ``flux help run``

       ``flux run --help``

       ``flux job kill --help``

     - Print a brief command summary.

       Display :man1:`flux-run`.

       Summarize run options.

       List usage and options for ``flux job kill`` sub-command.

   * - ``flux start -s16``

     -  Start a test instance that mocks 16 nodes.  See :man1:`flux-start`.

   * - ``flux version``

       ``flux uptime``

     - Print the Flux version.  See :man1:`flux-version`.

       Show brief Flux instance info.  See :man1:`flux-uptime`.

   * - ``flux ping 15``

       ``flux exec -x 0 cmd``

     - Bounce a message off broker rank 15.  See :man1:`flux-ping`.

       Run cmd on all ranks except rank 0.  Not a job. See :man1:`flux-exec`.

   * - ``flux resource info``

       ``flux resource list``

       ``flux resource status``

     - Show a single line summary of scheduler view of resources.

       Show longer scheduler view of resources.

       Show system view of resources.  See :man1:`flux-resource`.

   * - ``flux run sleep 5``

       ``flux run -N8 -n16 hostname``

       ``flux run -n64 hostname``

     - Run a job with 1 :linux:man1:`sleep` command.  Blocks until done.

       Run a job with 16 :linux:man1:`hostname` commands, two per node.

       Run a job with 64 tasks with 1 cpu per task.  See :man1:`flux-run`.

   * - ``flux submit -n64 -c2 hostname``

       ``flux submit --cc 1-5 sleep 30``

       ``flux watch --all``

     - Submit a job with 64 tasks, 2 cpus per task.  See :man1:`flux-submit`.

       Submit 5 jobs, each consisting of one :linux:man1:`sleep` task.

       Watch all job output and wait for completion.  See :man1:`flux-watch`.

   * - ``flux alloc -N4``

       ``flux bulksubmit sleep {} ::: 8 9``

       ``flux top``

     - Start an interactive 4 node instance.  See :man1:`flux-alloc`.

       Submit 2 jobs that sleep different times.  See :man1:`flux-bulksubmit`.

       View the progress of running jobs.  See :man1:`flux-top`.

   * - ``flux batch -N4 script.sh``

       ``flux batch -N1 --wrap sleep 60``

       ``flux pstree``

     - Submit job to run ``script.sh`` in a 4 node instance.

       Submit job to run :linux:man1:`sleep` in a 1 node instance.  See :man1:`flux-batch`.

       Display tree of running jobs by name.  See :man1:`flux-pstree`.

   * - ``flux jobs -A``

       ``flux jobs -a``

       ``flux jobs -o endreason ƒuAsjAo``

       ``flux job last``

     - List active jobs for all users.  See :man1:`flux-jobs`.

       List all my jobs (inactive too).

       Show info about the specified job including why it ended.

       Print my most recently submitted jobid.  See :man1:`flux-job`.

   * - ``flux cancel ƒuAsjAo``

       ``flux job kill -s HUP ƒuAsjAo``

       ``flux pgrep -f pending .``

       ``flux pkill sl..p``

     - Cancel specified job.  See :man1:`flux-cancel`.

       Send specified job a SIGHUP.  See :man1:`flux-job`.

       List ids of all pending jobs.  See :man1:`flux-pgrep`.

       Cancel all jobs named sleep or slurp.  See :man1:`flux-pkill`.

.. _connect_to_flux:

Connecting to Flux
------------------

Flux commands need a Flux instance to talk to.  Which one?  Remember that batch
jobs are Flux instances, allocations are Flux instances, and Slurm jobs can
even be Flux instances.  Complicating matters, Flux instances can be launched
recursively.

local URI
^^^^^^^^^

Each instance, or more properly each Flux broker within an instance, can
be contacted via a unique local URI.  The URI corresponds to a UNIX domain
socket and looks something like::

  local:///tmp/flux-lMDa6Z/local-0

In the :term:`initial program` (batch script, interactive alloc shell, or
whatever), the FLUX_URI environment variable is set to the local URI of the rank
0 broker.  Flux commands in the initial program, which also runs on rank 0,
read FLUX_URI and reference the instance that started them.

When running outside of an instance, FLUX_URI will not be set.  In this case,
commands fall back to the compiled-in URI of the Flux :term:`system instance`.
When there isn't a broker of the system instance running on the local node,
commands fail with an error like::

  ERROR: Unable to connect to Flux: broker socket /run/flux/local was not found

remote URI
^^^^^^^^^^

A Flux instance also has a remote URI that looks like::

  ssh://test3/tmp/flux-lMDacZ/local-0

This is the local URI above with the scheme changed to "ssh" and the hostname
"test3" prepended to the path.  Given a job ID, :man1:`flux-uri` can look up
the remote URI:

.. code-block:: console

  $ flux batch -N2 --wrap sleep 120
  ƒcbUvuHDCiB
  $ flux uri ƒcbUvuHDCiB
  ssh://test3/tmp/flux-gMypIR/local-0
  $

Which can be used as follows:

.. _sleep_example:

.. code-block:: console

  $ flux batch -N2 --wrap sleep 120
  ƒcbUvuHDCiB
  $ FLUX_URI=$(flux uri $(flux job last)) flux submit --cc 1-5 -N2 sleep 60
  ƒ3croSDd
  ƒ3ctHRVy
  ƒ3ctHRVz
  ƒ3cumQnK
  ƒ3cwFQ4f
  $ FLUX_URI=$(flux uri $(flux job last)) flux jobs
       JOBID USER     NAME       ST NTASKS NNODES     TIME INFO
    ƒ3ctHRVy alice    sleep       S      2      2        -
    ƒ3ctHRVz alice    sleep       S      2      2        -
    ƒ3cumQnK alice    sleep       S      2      2        -
    ƒ3cwFQ4f alice    sleep       S      2      2        -
    ƒ3croSDd alice    sleep       R      2      2   6.593s test[3-4]

That started a batch job with a lifetime of 120s, then submitted 5 "sleep 60"
jobs to it, then listed the batch job's active jobs.

parent URI
^^^^^^^^^^

Sometimes it's handy to direct a Flux command at the enclosing or parent
instance of the current Flux instance.  The :man1:`flux` command driver has
a ``--parent`` option which alters FLUX_URI to refer to the enclosing instance
in its sub-command's environment.

How would a batch job submit a cleanup job to run upon its completion?  The
cleanup job would be submitted to the enclosing instance rather than the
batch instance.  The batch script might do this:

.. code-block:: sh

  #!/bin/sh
  batch_jobid=$(flux getattr jobid)
  flux --parent submit --dependency afterany:$batch_jobid cleanup.sh

URI resolver
^^^^^^^^^^^^

:man1:`flux-uri` and some Flux commands employ an internal URI resolver class
that can use various tricks to find a usable remote URI for a Flux instance.
The input is an "unresolved URI" whose scheme selects the resolver method.
If no scheme is specified, the default is ``jobid``, thus the following commands
are equivalent::

  flux uri ƒcbUvuHDCiB
  flux uri jobid:ƒcbUvuHDCiB

A ``slurm`` scheme enables a Slurm job id to be resolved:

.. code-block:: console

  $ sbatch -N2 --wrap "srun flux start sleep 120"
  Submitted batch job 1533009
  $ flux uri slurm:1533009
  ssh://quartz17/var/tmp/bob/flux-xBj7Cg/local-0

Other schemes are available like ``pid`` and ``lsf``.

flux proxy
^^^^^^^^^^

It gets a bit tedious setting FLUX_URI for every command, plus each command
has to initiate a new connection to the remote broker which could be slow.
:man1:`flux-proxy` establishes a connection once, and spawns a shell with a
proxy FLUX_URI setting so that commands run within it work seamlessly with the
remote instance.  When the shell exits, the connection is dropped.
:man1:`flux-proxy` uses the URI resolver so its job ID argument can be an
unresolved URI.

The :ref:`example above <sleep_example>` can be simplified as follows:

.. code-block:: console

  $ flux batch -N2 --wrap sleep 120
  ƒcTzRVhnrW3
  $ flux proxy $(flux job last)
  ƒ(s=2,d=1) $ flux submit --cc 1-5 -N2 sleep 60
  ƒABfkxas
  ƒABfkxat
  ƒABhEwsD
  ƒABiiw9Z
  ƒABkCvRu
  ƒ(s=2,d=1) $ flux jobs
       JOBID USER     NAME       ST NTASKS NNODES     TIME INFO
    ƒABfkxat bob      sleep       S      2      2        -
    ƒABhEwsD bob      sleep       S      2      2        -
    ƒABiiw9Z bob      sleep       S      2      2        -
    ƒABkCvRu bob      sleep       S      2      2        -
    ƒABfkxas bob      sleep       R      2      2   2.028s test[3-4]
  ƒ(s=2,d=1) $ exit
  $

.. tip::

  This customized bash shell prompt is neat way to maintain your bearings
  in a Flux instance hierarchy. Add this to your ``.bashrc``:

  .. code-block:: shell

    if ! echo "$PS1" | grep -q FLUX; then
      PS1=$'${FLUX_URI+\u0192(s=$(flux getattr size),d=$(flux getattr instance-level)$(which flux|grep -q src/cmd && echo ,builddir))} '${PS1}
    fi

  ``ƒ(s=2,d=1)`` says you're in a Flux instance of size 2 at instance depth 1.

a proxy use case with Hydra
^^^^^^^^^^^^^^^^^^^^^^^^^^^

:man1:`flux-proxy` can provide interactive access to Flux when the start method
doesn't support it.  A few hints are in order for this use case:

- Make the initial program print the remote URI and then sleep indefinitely.

- Stop the instance with :man1:`flux-shutdown` when it is no longer needed.

- Beware that the interactive proxy shell will get a SIGHUP if the instance
  terminates while the proxy is in progress.  To avoid this, stop the instance
  *after* exiting the proxy shell.

- Note that unlike :man1:`flux-alloc`, the proxy shell runs locally, not on
  the first node of the instance.


With those points in mind, we can revisit the :ref:`start_hydra` example
and tweak it to be used interactively:

.. code-block:: console

  $ mpiexec.hydra -f hosts -launcher ssh flux start "flux uri --remote \$FLUX_URI; sleep inf"
  ssh://test0/tmp/flux-NCPWYE/local-0

Now in another window:

.. code-block:: console

  $ flux proxy ssh://test0/tmp/flux-NCPWYE/local-0
  ƒ(s=8,d=0) $ flux uptime
   09:41:03 run 42s,  owner bob,  depth 0,  size 8
  ƒ(s=8,d=0) $ exit
  exit
  $ flux shutdown --quiet ssh://test0/tmp/flux-NCPWYE/local-0
  broker.err[0]: rc2.0: flux uri --remote $FLUX_URI; sleep inf Hangup (rc=129) 52.2s
  $

The rc2 hangup error indicates that the initial program had to be terminated
by the shutdown sequence.  Normally that would be concerning, but it is expected
in this situation.
