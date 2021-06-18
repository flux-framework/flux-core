#!/bin/sh
#

# job-list test helper functions

# fj_wait_event
#
# flux job wait event w/ a timeout, to ensure tests can't hang
fj_wait_event() {
    flux job wait-event --timeout=20 "$@"
}

# state_ids
# - arg1 - state
#
# Return the expected jobids list in a given state/filter:
#   "all", "pending", "running", "inactive", "active",
#   "completed", "canceled", "failed"
#
# Note that while the above states are the "expected" states,
# technically any string to a ${filename}.ids can be passed to this
# function.
#
state_ids() {
    for f in "$@"; do
        cat ${f}.ids
    done
}

# state_count
# - arg1 - state
#
# Return the expected count of jobs in a given state (See above for
# common list)
#
# Note that while the above states are the "expected" states,
# technically any string to a ${filename}.ids can be passed to this
# function.
#
state_count() {
    state_ids "$@" | wc -l
}

# wait_jobid_state
# - arg1 - jobid
# - arg2 - job state
#
# Wait until a jobid reaches a specific state
wait_jobid_state() {
        local jobid=$(flux job id $1)
        local state=$2
        local i=0
        while ! flux job list --states=${state} | grep $jobid > /dev/null \
               && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

# wait_states
#
# the job-list module has eventual consistency with the jobs stored in
# the job-manager's queue.  To ensure no raciness in tests, we spin
# until all of the pending jobs have reached SCHED state, running jobs
# have reached RUN state, and inactive jobs have reached INACTIVE
# state.
wait_states() {
        pending=$(state_count pending)
        running=$(state_count running)
        inactive=$(state_count inactive)
        local i=0
        while ( [ "$(flux job list --states=sched | wc -l)" != "$pending" ] \
                || [ "$(flux job list --states=run | wc -l)" != "$running" ] \
                || [ "$(flux job list --states=inactive | wc -l)" != "$inactive" ]) \
               && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}
