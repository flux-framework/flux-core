#!/bin/sh

test_description='Test flux job info list services w/ changing job data'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

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

#
# job list tests
#
# these tests come first, as we do not want job submissions below to
# interfere with expected results
#

# submit a whole bunch of jobs for job list testing
#
# - the first loop of job submissions are intended to have some jobs run
#   quickly and complete
# - the second loop of job submissions are intended to eat up all resources
# - the last job submissions are intended to get a create a set of
#   pending jobs, because jobs from the second loop have taken all resources
#   - we desire pending jobs sorted in priority order, so we need to
#   create the sorted list for comparison later.
# - job ids are stored in files in the order we expect them to be listed
#   - pending jobs - by priority (highest first), submission time
#                    (earlier first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#
# TODO
# - alternate userid job listing

# Return the expected jobids list in a given state:
#   "all", "pending", "running", "inactive", "active",
#   "completed", "cancelled", "failed"
#
state_ids() {
    for f in "$@"; do
        cat ${f}.ids
    done
}

# Return the expected count of jobs in a given state (See above for list)
#
state_count() {
    state_ids "$@" | wc -l
}

# the job-info module has eventual consistency with the jobs stored in
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

test_expect_success 'submit jobs for job list testing' '
        #  Create `hostname` and `sleep` jobspec
        #
        flux jobspec --format json srun -N1 hostname > hostname.json &&
        flux jobspec --format json srun -N1 sleep 300 > sleeplong.json &&
        #
        # submit jobs that will complete
        #
        for i in $(seq 0 3); do \
                flux job submit hostname.json >> inactiveids; \
                fj_wait_event `tail -n 1 inactiveids` clean ; \
        done &&
        #
        #  Currently all inactive ids are "completed"
        #
        tac inactiveids | flux job id > completed.ids &&
        #
        #  Submit 8 sleep jobs to fill up resources
        #
        for i in $(seq 0 7); do
                flux job submit sleeplong.json >> runningids
        done &&
        tac runningids | flux job id > running.ids &&
        #
        #  Submit a set of jobs with misc priorities
        #
        id1=$(flux job submit -p20 hostname.json) &&
        id2=$(flux job submit      hostname.json) &&
        id3=$(flux job submit -p31 hostname.json) &&
        id4=$(flux job submit -p0  hostname.json) &&
        id5=$(flux job submit -p20 hostname.json) &&
        id6=$(flux job submit      hostname.json) &&
        id7=$(flux job submit -p31 hostname.json) &&
        id8=$(flux job submit -p0  hostname.json) &&
        flux job id $id3 > pending.ids &&
        flux job id $id7 >> pending.ids &&
        flux job id $id1 >> pending.ids &&
        flux job id $id5 >> pending.ids &&
        flux job id $id2 >> pending.ids &&
        flux job id $id6 >> pending.ids &&
        flux job id $id4 >> pending.ids &&
        flux job id $id8 >> pending.ids &&
        cat pending.ids > active.ids &&
        cat running.ids >> active.ids &&
        tac inactiveids | flux job id > inactive.ids &&
        cat active.ids > all.ids &&
        cat inactive.ids >> all.ids &&
        #
        #  Synchronize all expected states
        #
        wait_states
'

# Note: "running" = "run" | "cleanup", we also test just "run" state
# since we happen to know all these jobs are in the "run" state given
# checks above

test_expect_success HAVE_JQ 'flux job list running jobs in started order' '
        flux job list -s running | jq .id > list_started.out &&
        test_cmp list_started.out running.ids
'

test_expect_success HAVE_JQ 'flux job list inactive jobs in completed order' '
        flux job list -s inactive | jq .id > list_inactive.out &&
        test_cmp list_inactive.out inactive.ids
'

# Note: "pending" = "depend" | "sched", we also test just "sched"
# state since we happen to know all these jobs are in the "sched"
# state given checks above

test_expect_success HAVE_JQ 'flux job list pending jobs in priority order' '
        flux job list -s pending | jq .id > list_pending.out &&
        test_cmp list_pending.out pending.ids
'

test_expect_success HAVE_JQ 'flux job list pending jobs with correct priority' '
        cat >list_priority.exp <<-EOT &&
31
31
20
20
16
16
0
0
EOT
        flux job list -s pending | jq .priority > list_priority.out &&
        test_cmp list_priority.out list_priority.exp
'

test_expect_success HAVE_JQ 'change a job priority' '
        jobid=`head -n 1 pending.ids` &&
        flux job priority ${jobid} 1
'

test_expect_success HAVE_JQ 'wait for job-info to see job priority change' '
        jobidhead=`head -n 6 pending.ids | tail -n 5 > pending_after.ids` &&
        jobidchange=`head -n 1 pending.ids >> pending_after.ids` &&
        jobidend=`tail -n2 pending.ids >> pending_after.ids` &&
        i=0 &&
        while flux job list -s pending | jq .id > list_pending_after.out && \
              ! test_cmp list_pending_after.out pending_after.ids && \
              [ $i -lt 5 ]
        do
                sleep 1
                i=$((i + 1))
        done &&
        test "$i" -lt "5" &&
        flux job list -s pending | jq .id > list_pending_after.out &&
        test_cmp list_pending_after.out pending_after.ids
'

test_expect_success HAVE_JQ 'flux job list pending jobs with correct priority' '
        cat >list_priority_after.exp <<-EOT &&
31
20
20
16
16
1
0
0
EOT
        flux job list -s pending | jq .priority > list_priority_after.out &&
        test_cmp list_priority_after.out list_priority_after.exp
'

test_expect_success 'cleanup job listing jobs ' '
        for jobid in `cat active.ids`; do \
            flux job cancel $jobid; \
            fj_wait_event $jobid clean; \
        done
'

test_done
