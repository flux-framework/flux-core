.. list-table::
   :header-rows: 1

   * - Name
     - Description

   * - :envvar:`FLUX_JOB_ID`
     - the current jobid

   * - :envvar:`FLUX_JOB_SIZE`
     - the number of tasks in the current job

   * - :envvar:`FLUX_JOB_NNODES`
     - the total number of nodes hosting tasks on behalf of the job

   * - :envvar:`FLUX_TASK_RANK`
     - the global task rank (zero origin)

   * - :envvar:`FLUX_TASK_LOCAL_ID`
     - the local task rank (zero origin)

   * - :envvar:`FLUX_JOB_TMPDIR`
     - path to temporary, per-job directory, usually in ``/tmp``

   * - :envvar:`FLUX_URI`
     - the URI of the enclosing Flux instance
