JOB ENVIRONMENT VARIABLES
=========================

Flux creates several environment variables for every job task.  They are as follows:

**FLUX_JOB_ID**
    The jobid for this job

**FLUX_URI**
    The URI of the enclosing Flux instance

**FLUX_JOB_TMPDIR**
    Path to temporary directory created for job

**FLUX_TASK_LOCAL_ID**
    The task id local to the node

**FLUX_TASK_RANK**
    The global rank for this task

**FLUX_JOB_NNODES**
    The total number of nodes in the job

**FLUX_JOB_SIZE**
    The count of job shells in the job, typical the same value as FLUX_JOB_NNODES

**FLUX_KVS_NAMESPACE**
    The identifier of the KVS guest namespace of the job. This redirects KVS command and
    API calls to the job guest namespace instead of the root namespace.
