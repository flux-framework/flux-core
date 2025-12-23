Starting a Flux Instance
========================

A *Flux instance* is a self-contained workload manager.  It is so easy to
start a new Flux instance that we often ask ourselves "can we just start a
new instance?" before we consider extending Flux to solve a new problem.

In a broad sense, Flux is a workload manager on a par with Slurm, Torque,
LSF, or similar systems.  It distinguishes itself by being able to be started
standalone, as a job in another system, or a job in *itself* (recursively).
It doesn't require elevated privileges to do so for one user at a time, so
anyone can download Flux and immediately use it productively.  This opens up
possibilities for using Flux as a portability layer or "step scheduler" in
workflow systems, and for cooking up divide and conquer solutions to
resource and workload problems.

A Flux instance consists of a set of services running on top of a distributed
message broker.  To start Flux, we are just starting one or more brokers in
parallel, as a *parallel program* if you will.

:man1:`flux-start` starts a Flux instance, whose life cycle consists of three
phases:

#. Initialize
#. The rank 0 broker runs the :term:`initial program` to completion
#. Finalize

By default, the initial program is an interactive shell.  If :man1:`flux-start`
is passed free arguments, they are interpreted as commands to run
non-interactively instead.  Either way, when the initial program terminates,
the instance terminates.

.. _test_instance:

Starting a Test Instance
------------------------

A standalone Flux instance can be started right away, say on your laptop, or
on a login node of your institution's big cluster.  This is a convenient way to
begin experimenting.

.. code-block:: console

  $ flux start --test-size=3
  $ flux uptime
   23:50:28 run 1.3m,  owner alice,  depth 0,  size 3
  $ flux resource info
  3 Nodes, 12 Cores, 0 GPUs
  $ flux run --label-io -N3 hostname
  2: test0
  1: test0
  0: test0
  $ exit
  $

What just happened?  Hint: the process tree looks like this:

.. code-block:: console

  flux-start─┬─flux-broker-0───bash
             └─flux-broker-1
             └─flux-broker-2

Let's break it down:

#. :man1:`flux-start` launches three Flux brokers on the local system,
   ranked 0, 1, and 2.

#. The brokers find their rank and exchange peer network addresses and public
   keys using a `PMI server <https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_13.html>`_
   offered by the start command, which is the process parent of the brokers.
   The PMI server is only offered by :man1:`flux-start` when the
   ``--test-size=N`` option is used.

#. After the brokers are connected and have completed initialization, the
   rank 0 broker spawns the initial program, an interactive shell that has its
   environment set up so that Flux commands run from it will connect to the
   new Flux instance.  The prompt right after the start command is from this
   new shell.

#. We run :man1:`flux-uptime` to get some basic info about the system and note
   that it consists of three brokers, and that it has no parent instance, hence
   a "depth" of zero.

#. We run :man1:`flux-resource` *list* to show the instance's resource
   inventory.  In this contrived test instance, we have three brokers running
   on one node, but each broker assumes that it is discovering the resources
   of a separate node, so while there is really 1 node, 4 cores, the
   resource inventory contains 3 nodes, 12 cores.  See below for more on that.

#. :man1:`flux-run` launches the :linux:man1:`hostname` command in
   parallel across the three "nodes" and waits for it to complete.
   We observe that each reports the same hostname.

#. Finally, we exit the interactive shell.  Since that was the initial program,
   the instance exits.  The next prompt is the original shell, before we ran
   the start command.

It is convenient to be able to start a Flux instance like this in pretty much
any environment, but to reinforce what was alluded to above, the main
caveat of a test instance is that it doesn't actually have exclusive access
to resources.  `HWLOC <https://www.open-mpi.org/projects/hwloc/>`_ is used to
discover what hardware is out there on each broker, but the broker has no way
to claim it exclusively.  A test instance on a shared login node provides the
illusion of exclusive access to jobs run within it, but someone else running
a test instance on the same node may be doing the same thing.  Furthermore,
when the test size is greater than one, the actual hardware resources may be
oversubscribed within the single instance.

Let's create a script to run as the initial program in place of the interactive
shell.  Call this script ``workload.sh``.  It runs the same commands as were
run above, in a way that works on any size instance.

.. code-block:: shell

  #!/bin/sh
  flux resource info
  flux uptime
  NNODES=$(flux resource list -no {nnodes})
  flux run --label-io -N $NNODES hostname

When we run this way, we get the same result as above, but the user doesn't
have to provide any input while the instance is running:

.. code-block:: console

  $ flux start --test-size=3 ./workload.sh
  3 Nodes, 12 Cores, 0 GPUs
   10:00:30 run 2.9s,  owner alice,  depth 0,  size 3
  0: test0
  2: test0
  1: test0
  $

This is how many of Flux-core's tests work.  The initial program is a test
script run under a test instance started by the test suite.

.. _start_slurm:

Starting with Slurm
-------------------

When :man1:`flux-start` is run without the ``--test-size=N`` option, it
simply execs the broker rather than sticking around to provide PMI service.
The broker uses its PMI client to determine its place in a parallel program,
or if PMI is not found, it runs as a singleton.

So to start a Flux instance in a foreign resource manager, we just run
:man1:`flux-start` in parallel as though it were an MPI job, usually with
one broker per node in the foreign allocation.

.. note::
  Slurm has a few different options that control the parallel environment
  for jobs.  If your site has not configured ``MpiDefault=pmi2``, then it may
  be necessary to run flux with the srun ``--mpi=pmi2`` option.  If a parallel
  launch of Flux results in multiple singletons, e.g. reporting 1 node when
  more were expected, this may help.

In Slurm, we can obtain an allocation with `salloc(1)
<https://slurm.schedmd.com/salloc.html>`_.  From the interactive shell
it spawns, we use `srun(1) <https://slurm.schedmd.com/srun.html>`_
to start a Flux instance.  The ``--pty`` option gives Flux's interactive
initial program a proper terminal.

.. code-block:: console

  $ salloc -N2
  salloc: Pending job allocation 1505790
  salloc: job 1505790 queued and waiting for resources
  salloc: job 1505790 has been allocated resources
  salloc: Granted job allocation 1505790
  salloc: Waiting for resource configuration
  salloc: Nodes quartz[2257-2258] are ready for job
  $ srun -N2 --pty flux start
  $ flux uptime
   10:04:05 run 6.6s,  owner alice,  depth 0,  size 2
  $ flux resource info
  2 Nodes, 72 Cores, 0 GPUs
  $ flux run -N2 hostname
  quartz2257
  quartz2258
  $ exit
  $ exit
  salloc: Relinquishing job allocation 1505790
  salloc: Job allocation 1505790 has been revoked.
  $

After typing the same two Flux commands that were demonstrated in
:ref:`test_instance`, we exit the Flux interactive shell, then the
Slurm one, which gives up the Slurm allocation.

Conceptually, what's going on here is Slurm has granted a resource
allocation to Flux, which then parcels it out to the jobs submitted to it.

The above can be accomplished a little more succinctly by just running
srun directly.  Then the session looks like this:

.. code-block:: console

  $ srun -N2 --pty flux start
  srun: job 1505791 queued and waiting for resources
  srun: job 1505791 has been allocated resources
  $ flux uptime
   10:05:51 run 4.7s,  owner alice,  depth 0,  size 2
  $ flux resource info
  2 Nodes, 72 Cores, 0 GPUs
  $ flux run --label-io -N2 hostname
  0: quartz2257
  1: quartz2258
  $ exit
  $

To run non-interactively, we can drop the ``--pty`` option and use our
workload script for the initial program:

.. code-block:: console

  $ srun -N2 flux start ./workload.sh
  srun: job 1505795 queued and waiting for resources
  srun: job 1505795 has been allocated resources
   10:07:12 run 4.6s,  owner alice,  depth 0,  size 2
  2 Nodes, 72 Cores, 0 GPUs
  0: quartz25
  1: quartz26

Finally, Slurm batch execution of a Flux workload can be accomplished
by wrapping the srun command in a batch script we will name ``batch.sh``:

.. code-block:: shell

  #!/bin/sh
  #SBATCH -N2
  srun flux start ./workload.sh

Whee!

.. code-block:: console

  $ sbatch ./batch.sh
  Submitted batch job 1505846
  $ cat slurm-1505846.out
   10:09:57 run 4.8s,  owner alice,  depth 0,  size 2
  2 Nodes, 72 Cores, 0 GPUs
  0: quartz47
  1: quartz48
  $

Inserting Flux between Slurm and a real workload script or workflow executor
could have some advantages, such as:

- Workload scripts that use Flux commands or the Flux python API can be made
  portable to other systems that don't use Slurm as their primary resource
  manager, as long as they can launch a Flux instance.

- High throughput workloads are less of a burden on the Slurm controller
  and may run faster compared to the same workload run with job steps, since
  `Step Allocation <https://slurm.schedmd.com/job_launch.html#step_allocation>`_
  involves an RPC to the Slurm controller.

- When combined with the `flux-sched project
  <https://github.com/flux-framework/flux-sched>`_, Flux offers more
  sophisticated step scheduling.

- A Flux instance may be tailored by the instance owner to meet the specific
  demands of the workload.  For example, the scheduler algorithm may be
  changed.

To be clear, Flux does not have "steps" per se, only jobs.   We don't need them
since Flux batch jobs are just regular Flux jobs that happen to be independent
Flux instances, and Flux instances have the capability to run more Flux jobs.

Since Slurm runs on many of the largest HPC systems, the capability to launch
Flux instances under Slurm provided early opportunities to work on Flux
portability and scalability on those systems without requiring much buy-in from
the system owners, other than permission to run there.

Starting with Flux
------------------

Flux can run parallel programs, and a Flux instance can be run as one, so Flux
can run Flux.  No surprise there.  What does that look like and how is it
leveraged to provide expected workload manager abstractions?

First let's try our workload commands in the "outer" Flux instance, in this
case a standalone :term:`system instance`, although this section applies to all
Flux instances.

.. code-block:: console

  $ flux uptime
   10:13:27 run 23d,  owner flux,  depth 0,  size 101,  7 drained,  8 offline
  $ flux resource info
  98 Nodes, 392 Cores, 0 GPUs
  $ flux run -N2 hostname
  $ flux run --label-io -N2 hostname
  0: fluke6
  1: fluke7

The instance depth is zero since this is the system instance.  We also get a
heads up about some unavailable nodes, and note that the instance owner is
the ``flux`` user, but otherwise this looks like before.  Now, an interactive
Flux allocation:

.. code-block:: console

  $ flux alloc -N2
  $ flux uptime
   10:16:58 run 7.3s,  owner alice,  depth 1,  size 2
  $ flux resource info
  2 Nodes, 8 Cores, 0 GPUs
  $ flux run -N2 hostname
  fluke6
  fluke7
  $ exit
  $

The :man1:`flux-alloc` command spawns a new Flux instance in the allocation
automatically.  The instance depth is now 1.  This is an example of how Flux's
recursive launch property simplifies its design:  instead of requiring
specialized code to support interactive allocations, Flux just transfers the
requested resources to a new instance that runs for the life of its initial
program, the interactive shell.

As a side note, while :man1:`flux-alloc` provides an interface that is
convenient as well as familiar to Slurm users, we could have accomplished
the same thing with :man1:`flux-run` and :man1:`flux-start`:

.. code-block:: console

  $ flux run -N2 -o pty.interactive flux start
  $ flux uptime
   10:19:27 run 8.3s,  owner alice,  depth 1,  size 2
  $ flux resource info
  2 Nodes, 8 Cores, 0 GPUs
  $ flux run --label-io -N2 hostname
  0: fluke6
  1: fluke7
  $ exit
  [detached: session exiting]
  $

Now let's try batch:

.. code-block:: console

  $ flux batch -N2 ./workload.sh
  f2SoPsfdA7Ub
  $ cat flux-f2SoPsfdA7Ub.out
   10:25:05 run 4.8s,  owner alice,  depth 1,  size 2
  2 Nodes, 8 Cores, 0 GPUs
  0: fluke6
  1: fluke7

Like the allocation example, the :man1:`flux-batch` command spawns a new Flux
instance that runs for the duration of its initial program, the batch script
``workload.sh``.  The Flux design does not require job steps or a specialized
step scheduler because of the recursive launch property.

Although :man1:`flux-batch` has other nice features like support for batch
directives and copying of the batch script at submission, a similar result
can be obtained for this simple example with :man1:`flux-submit` and
:man1:`flux-start`:

.. code-block:: console

  $ flux submit --output=flux-{{id}}.out -N2 flux start ./workload.sh
  f2SoiFq3N5Jo
  $ cat flux-f2SoiFq3N5Jo.out
   11:05:24 run 4.7s,  owner alice,  depth 1,  size 2
  2 Nodes, 8 Cores, 0 GPUs
  0: fluke6
  1: fluke7

.. _start_hydra:

Starting with Hydra
-------------------

MPICH `hydra <https://github.com/pmodels/mpich/blob/main/doc/wiki/how_to/Using_the_Hydra_Process_Manager.md>`_
is an MPI launcher with PMI support that can start a Flux instance across
multiple nodes using ssh.  It is sometimes just the right tool needed to get
a Flux instance going when nothing else will work.

Given a ``hosts`` file containing:

::

  test0
  test1
  test2
  test3
  test4
  test5
  test6
  test7

And assuming passwordless ssh works for those nodes, one can run:

.. code-block:: console

  $ mpiexec.hydra -launcher ssh -f hosts flux start ./workload.sh
   23:23:14 run 3.9s,  owner alice,  depth 0,  size 8
  8 Nodes, 32 Cores, 0 GPUs
  0: test0
  2: test2
  1: test1
  5: test5
  6: test6
  7: test7
  4: test4
  3: test3

Starting with Static Configuration
----------------------------------

In the methods described above, Flux brokers determine their ranks and
exchange peer network addresses and public keys using PMI.  In situations
where PMI is unavailable or inappropriate, the same information can be loaded
from static configuration files.  As an example, we'll start Flux on the same
eight nodes from :ref:`start_hydra`.

Generate a shared CURVE certificate that will be used to secure the overlay
network:

.. code-block:: console

  $ mkdir test
  $ cd test
  $ flux keygen test.cert
  $

Create a TOML config file as described in :man5:`flux-config-bootstrap`.
We'll call it ``test.toml``.  For this experiment, we can place it in the
same directory as ``test.cert``:

.. code-block:: toml

  [bootstrap]
  curve_cert = "/home/bob/test/test.cert"

  hosts = [
    { host="test0", bind="tcp://eth0:8060", connect="tcp://test0:8060" },
    { host="test1", bind="tcp://eth0:8060", connect="tcp://test1:8060" },
    { host="test2", bind="tcp://eth0:8060", connect="tcp://test2:8060" },
    { host="test3", bind="tcp://eth0:8060", connect="tcp://test3:8060" },
    { host="test4", bind="tcp://eth0:8060", connect="tcp://test4:8060" },
    { host="test5", bind="tcp://eth0:8060", connect="tcp://test5:8060" },
    { host="test6", bind="tcp://eth0:8060", connect="tcp://test6:8060" },
    { host="test7", bind="tcp://eth0:8060", connect="tcp://test7:8060" },
  ]

If necessary, replicate the ``test`` directory across the eight nodes.

Finally, start a broker on each node.  You can use any technique you like,
but keep in mind the brokers will not exit until the initial program completes,
and the initial program will not start until all the brokers are online.
We use :linux:man1:`pdsh` to start the brokers in parallel and capture any
errors they print to the standard error stream:

.. code-block:: console

  $ pdsh -N -w test[0-7] flux start -o,--config-path=/home/bob/test/test.toml ./workload.sh
  8 Nodes, 32 Cores, 0 GPUs
   00:03:31 run 2.5s,  owner bob,  depth 0,  size 8
  0: test0
  6: test6
  7: test7
  4: test4
  5: test5
  1: test1
  3: test3
  2: test2

This method of starting Flux is used when `systemd <https://systemd.io/>`_
starts brokers belonging to a Flux system instance.  This is described in the
`Flux Administration Guide
<https://flux-framework.readthedocs.io/en/latest/guides/admin-guide.html>`_.
Another example is the `Flux Operator
<https://flux-framework.org/flux-operator/>`_, which uses dynamically generated
configuration to bootstrap Flux in a Kubernetes cluster.

When compared with PMI bootstrap, this method of starting Flux has the
following drawbacks:

- Managing and sharing the instance-specific files is an extra step.

- The CURVE certificate must be protected from disclosure to parties other
  than the Flux instance owner, or an attacker may attach and impersonate
  the owner.

- The use of a fixed port number in the configuration raises the possibility
  that the port may already be in use on one of the nodes, especially if the
  same configuration file is reused.  If a broker cannot bind to its port,
  it will fail to start.

- If a broker fails to start, the ``pdsh`` command line above may hang.
  Options are available to deal with this like the ``broker.quorum`` setting
  described in :man7:`flux-broker-attributes`.
