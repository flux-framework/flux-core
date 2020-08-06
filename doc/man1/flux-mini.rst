.. flux-help-include: true

============
flux-mini(1)
============


SYNOPSIS
========

**flux** **mini** **submit** [OPTIONS] [*--ntasks=N*] COMMAND...

**flux** **mini** **run** [OPTIONS] [*--ntasks=N*] COMMAND...

**flux** **mini** **batch** [OPTIONS] *--nslots=N* SCRIPT...

**flux** **mini** **alloc** [OPTIONS] *--nslots=N* [COMMAND...]

DESCRIPTION
===========

flux-mini(1) submits jobs to run under Flux. In the case of **submit**
or **run** the job consists of *N* copies of COMMAND launched together
as a parallel job, while **batch** and **alloc** submit a script or launch
a command as the initial program of a new Flux instance.

If *--ntasks* is unspecified, a value of *N=1* is assumed. Commands that
take *--nslots* have no default and require that *--nslots* be explicitly
specified.

The **submit** and **batch** commands enqueue the job and print its numerical
Job ID on standard output.

The **run** and **alloc** commands do the same interactively, blocking until
the job has completed.

For **flux-mini batch**, the SCRIPT given on the command line is assumed
to be a file name, unless the *--wrap* option used, and the script
file is read and submitted along with the job. If no SCRIPT is
provided, then one will be read from *stdin*.

**flux-mini alloc** works similarly to **batch**, but instead blocks until
the job has started and interactively attaches to the new Flux instance.
By default, a new shell is spawned as the initial program of the instance,
but this may be overridden by supplying COMMAND on the command line.

The intent is for the "mini" commands to remain simple with stable interfaces
over time, making them suitable for use in scripts. For advanced usage,
see flux-run(1) and flux-submit(1).

The available OPTIONS are detailed below.


JOB PARAMETERS
==============

These commands accept only the simplest parameters for expressing
the size of the parallel program and the geometry of its task slots:

The **run** and **submit** commands take the following options to specify
the size of the job request:

**-n, --ntasks=N**
   Set the number of tasks to launch (default 1).

**-c, --cores-per-task=N**
   Set the number of cores to assign to each task (default 1).

**-g, --gpus-per-task=N**
   Set the number of GPU devices to assign to each task (default none).

The **batch** and **alloc** commands do not launch tasks directly, and
therefore job parameters are specified in terms of resource slot size
and number of slots. A resource slot can be thought of as the minimal
resources required for a virtual task. The default slot size is 1 core.

**-n, --nslots=N**
   Set the number of slots requested. This parameter is required.

**-c, --cores-per-slot=N**
   Set the number of cores to assign to each slot (default 1).

**-g, --gpus-per-slot=N**
   Set the number of GPU devices to assign to each slot (default none).

The **run**, **submit**, **batch**, and **alloc** commands also take
following additional job parameters:

**-N, --nodes=N**
   Set the number of nodes to assign to the job. Tasks will be distributed
   evenly across the allocated nodes. It is an error to request more nodes
   than there are tasks. If unspecified, the number of nodes will be chosen
   by the scheduler.

**-t, --time-limit=FSD**
   Set a time limit for the job in Flux standard duration (RFC 23).
   FSD is a floating point number with a single character units suffix
   ("s", "m", "h", or "d"). If unspecified, the job is subject to the
   system default time limit.


STANDARD I/O
============

By default, task stdout and stderr streams are redirected to the
KVS, where they may be accessed with the ``flux job attach`` command.

In addition, ``flux-mini run`` processes standard I/O in real time,
emitting the job's I/O to its stdout and stderr.

**--output=FILENAME**
   Redirect stdout to the specified FILENAME, bypassing the KVS.
   The mustache template *{{id}}* is expanded to the numerical Job ID,
   useful to ensure FILENAME is unique across multiple jobs. For
   **flux mini batch** the default for FILENAME is *flux-{{id}}.out*.
   To force output to go to KVS so it is available with ``flux job attach``,
   set FILENAME to *none* or *kvs*.

**--error=FILENAME**
   Redirect stderr to the specified FILENAME, bypassing the KVS.
   The mustache template *{{id}}* is expanded as above.

**-l, --label-io**
   Add task rank prefixes to each line of output.


EXIT STATUS
===========

The job exit status, normally the largest task exit status, is stored
in the KVS. If one or more tasks are terminated with a signal,
the job exit status is 128+signo.

The ``flux-job attach`` command exits with the job exit status.

In addition, ``flux-mini run`` runs until the job completes and exits
with the job exit status.


OTHER OPTIONS
=============

**--priority=N**
   Specify job priority, which affects queue order. Numerically higher priority
   jobs are considered by the scheduler first. Guests may submit jobs with
   priority in the range of 0 to 16, while instance owners may submit jobs
   with priority in the range of 0 to 31 (default 16).

**-v, --verbose**
   *(run only)* Increase verbosity on stderr. For example, currently ``-v``
   displays jobid, ``-vv`` displays job events, and ``-vvv`` displays exec events.
   The specific output may change in the future.

**-o, --setopt=KEY[=VAL]**
   Set shell option. Keys may include periods to denote hierarchy.
   VAL is optional and may be valid JSON (bare values, objects, or arrays),
   otherwise VAL is interpreted as a string. If VAL is not set, then the
   default value is 1. See SHELL OPTIONS below.

**--setattr=KEY=VAL**
   Set jobspec attribute. Keys may include periods to denote hierarchy.
   VAL may be valid JSON (bare values, objects, or arrays), otherwise VAL
   is interpreted as a string.

**--dry-run**
   Don't actually submit the job. Just emit jobspec on stdout and exit.

**--debug**
   Enable job debug events, primarily for debugging Flux itself.
   The specific effects of this option may change in the future.

**-B, --broker-opts=OPT**
   *(batch only)* For batch jobs, pass specified options to the Flux brokers
   of the new instance. This option may be specified multiple times.

**--wrap**
   *(batch only)* The ``--wrap`` option wraps the specified COMMAND and ARGS in
   a shell script, by prefixing with ``#!/bin/sh``. If no COMMAND is present,
   then a SCRIPT is read on stdin and wrapped in a /bin/sh script.


SHELL OPTIONS
=============

These options are provided by built-in shell plugins that may be
overridden in some cases:

**mpi=spectrum**
   Load the MPI personality plugin for IBM Spectrum MPI. All other MPI
   plugins are loaded by default.

**cpu-affinity=per-task**
   Tasks are distributed across the assigned resources.

**cpu-affinity=off**
   Disable task affinity plugin.

**gpu-affinity=per-task**
   GPU devices are distributed evenly among local tasks. Otherwise,
   GPU device affinity is to the job.

**gpu-affinity=off**
   Disable GPU affinity for this job.

**verbose**
   Increase verbosity of the job shell log.


RESOURCES
=========

Github: http://github.com/flux-framework
