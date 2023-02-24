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

**nosetpgrp**
   Normally the job shell runs each task in its own process group to
   facilitate delivering signals to tasks which may call :linux:man2:`fork`.
   With this option, the shell avoids calling :linux:man2:`setpgrp`, and
   each task will run in the process group of the shell. This will cause
   signals to be delivered only to direct children of the shell.

**pmi=off**
   Disable the process management interface (PMI-1) which is required for
   bootstrapping most parallel program environments.  See :man1:`flux-shell`
   for more pmi options.

**stage-in**
   Copy files previously mapped with :man1:`flux-filemap` to $FLUX_JOB_TMPDIR.
   See :man1:`flux-shell` for more *stage-in* options.

