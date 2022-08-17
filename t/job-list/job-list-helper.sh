#!/bin/sh
#

# job-list test helper functions

JOB_LIST_WAIT_ITERS=50

# Return the expected jobids list in a given state:
#   "all", "pending", "running", "inactive", "active",
#   "completed", "canceled", "failed", "timeout"
#
job_list_state_ids() {
    for f in "$@"; do
        cat ${f}.ids
    done
}

# Return the expected count of jobs in a given state (See above for list)
#
job_list_state_count() {
    job_list_state_ids "$@" | wc -l
}

# the job-list module has eventual consistency with the jobs stored in
# the job-manager's queue.  To ensure no raciness in tests, we spin
# until all of the pending jobs have reached SCHED state, running jobs
# have reached RUN state, and inactive jobs have reached INACTIVE
# state.
#
# job ids for jobs in these states are expected to be in pending.ids,
# running.ids, and inactive.ids respectively.

job_list_wait_states() {
        pending=$(job_list_state_count pending)
        running=$(job_list_state_count running)
        inactive=$(job_list_state_count inactive)
        local i=0
        while ( [ "$(flux job list --states=sched | wc -l)" != "$pending" ] \
                || [ "$(flux job list --states=run | wc -l)" != "$running" ] \
                || [ "$(flux job list --states=inactive | wc -l)" != "$inactive" ]) \
               && [ $i -lt ${JOB_LIST_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "${JOB_LIST_WAIT_ITERS}" ]
        then
            return 1
        fi
        return 0
}
