SHELL OPTIONS
=============

These options are provided by built-in shell plugins that may be
overridden in some cases:

.. option:: -o mpi=spectrum

   Load the MPI personality plugin for IBM Spectrum MPI. All other MPI
   plugins are loaded by default.

.. option:: -o cpu-affinity=per-task

   Tasks are distributed across the assigned resources.

.. option:: -o cpu-affinity=off

   Disable task affinity plugin.

.. option:: -o gpu-affinity=per-task

   GPU devices are distributed evenly among local tasks. Otherwise,
   GPU device affinity is to the job.

.. option:: -o gpu-affinity=off

   Disable GPU affinity for this job.

.. option:: -o verbose

   Increase verbosity of the job shell log.

.. option:: -o nosetpgrp

   Normally the job shell runs each task in its own process group to
   facilitate delivering signals to tasks which may call :linux:man2:`fork`.
   With this option, the shell avoids calling :linux:man2:`setpgrp`, and
   each task will run in the process group of the shell. This will cause
   signals to be delivered only to direct children of the shell.

.. option:: -o pmi=off

   Disable the process management interface (PMI-1) which is required for
   bootstrapping most parallel program environments.  See :man1:`flux-shell`
   for more pmi options.

.. option:: -o stage-in

   Copy files previously mapped with :man1:`flux-filemap` to $FLUX_JOB_TMPDIR.
   See :man1:`flux-shell` for more *stage-in* options.

