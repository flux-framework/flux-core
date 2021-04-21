#!/bin/sh

test_description='Test flux job list purge inactive'

. $(dirname $0)/job-list/job-list-helper.sh

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc

get_completed_count() {
    count=`flux jobs --stats-only 2>&1 | awk '{print \$3}'`
    echo ${count}
}

get_failed_count() {
    count=`flux jobs --stats-only 2>&1 | awk '{print \$5}'`
    echo ${count}
}

# submit a whole bunch of jobs for job list testing
#
# - the first loop of job submissions are intended to have some jobs run
#   quickly and complete
# - the second loop of job submissions are intended to eat up all resources
# - job ids are stored in files in the order we expect them to be listed
#   - pending jobs - by priority (highest first), job id (smaller first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#
# TODO
# - alternate userid job listing

test_expect_success 'submit jobs for testing' '
        #  Create `hostname` and `sleep` jobspec
        #  N.B. Used w/ `flux job submit` for serial job submission
        #  for efficiency (vs serial `flux mini submit`.
        #
        flux mini submit --dry-run hostname >hostname.json &&
        flux mini submit --dry-run --time-limit=5m sleep 600 > sleeplong.json &&
        #
        # submit jobs that will complete
        #
        for i in $(seq 0 3); do
                flux job submit hostname.json >> inactiveids
                fj_wait_event `tail -n 1 inactiveids` clean
        done &&
        #
        #  Currently all inactive ids are "completed"
        #
        tac inactiveids | flux job id > completed.ids &&
        #
        #  Run jobs that will fail, copy its JOBID to both inactive and
        #   failed lists.
        #
        ! jobid=`flux mini submit --wait nosuchcommand` &&
        echo $jobid >> inactiveids &&
        flux job id $jobid >> failedids &&
        jobid=`flux mini submit --time-limit=0.25 sleep 30` &&
        fj_wait_event $jobid clean &&
        echo $jobid >> inactiveids &&
        flux job id $jobid >> failedids &&
        #
        #  Submit 8 sleep jobs to fill up resources
        #
        for i in $(seq 0 7); do
                flux job submit sleeplong.json >> runningids
                fj_wait_event $jobid alloc
        done &&
        tac runningids | flux job id > running.ids &&
        #
        #  Submit a job and cancel it, copy its JOBID to both inactive
        #    and failed lists
        #
        jobid=`flux mini submit --job-name=canceledjob sleep 30` &&
        flux job wait-event $jobid depend &&
        flux job cancel $jobid &&
        flux job wait-event $jobid clean &&
        flux job id $jobid >> inactiveids &&
        flux job id $jobid >> failedids &&
        tac inactiveids | flux job id > inactive.ids &&
        tac failedids | flux job id > failed.ids &&
        # no pending jobs, create empty file of pending job ids
        touch pending.ids &&
        #
        #  Synchronize all expected states
        #
        wait_states
'

test_expect_success HAVE_JQ 'flux job list all inactive jobs' '
        flux job list -s inactive | jq .id > list_inactive1.out &&
        test_cmp list_inactive1.out inactive.ids
'

test_expect_success HAVE_JQ 'flux job list-ids lists all jobs' '
        ids=`cat inactive.ids | xargs` &&
        flux job list-ids ${ids} | jq .id > list_inactive2.out &&
        test_cmp list_inactive2.out inactive.ids
'

test_expect_success HAVE_JQ 'job-list stats has correct counts' '
        count=`flux module stats --parse "jobs.inactive" job-list` &&
        test ${count} -eq $(state_count inactive) &&
        count=`get_completed_count` &&
        test ${count} -eq $(state_count completed) &&
        count=`get_failed_count` &&
        test ${count} -eq $(state_count failed)
'

test_expect_success HAVE_JQ 'flux job list-purge 2 jobs' '
        flux job list-purge --count=2
'

test_expect_success HAVE_JQ 'remove ids from files that should have been purged' '
        count=$(state_count completed) &&
        count=$((count - 2)) &&
        head -n ${count} completed.ids > completed-purge2.ids &&
        count=$(state_count inactive) &&
        count=$((count - 2)) &&
        head -n ${count} inactive.ids > inactive-purge2.ids
'

test_expect_success HAVE_JQ 'flux job list inactive jobs updated' '
        flux job list -s inactive | jq .id > list_inactive3.out &&
        test_cmp list_inactive3.out inactive-purge2.ids
'

test_expect_success HAVE_JQ 'flux job list-ids still works' '
        ids=`cat inactive-purge2.ids | xargs` &&
        flux job list-ids ${ids} | jq .id > list_inactive4.out &&
        test_cmp list_inactive4.out inactive-purge2.ids
'

test_expect_success HAVE_JQ 'flux job list-ids fails on purged job ids' '
        id1=`tail -n 1 inactive.ids` &&
        test_must_fail flux job list-ids ${id1} &&
        id2=`tail -n 2 inactive.ids | head -n 1` &&
        test_must_fail flux job list-ids ${id2}
'

test_expect_success HAVE_JQ 'job-list stats has updated counts' '
        count=`flux module stats --parse "jobs.inactive" job-list` &&
        test ${count} -eq $(state_count inactive-purge2) &&
        count=`get_completed_count` &&
        test ${count} -eq $(state_count completed-purge2) &&
        count=`get_failed_count` &&
        test ${count} -eq $(state_count failed)
'

test_expect_success HAVE_JQ 'flux job list-purge all jobs' '
        flux job list-purge
'

test_expect_success HAVE_JQ 'flux job list inactive jobs lists 0 jobs' '
        count=`flux job list -s inactive | wc -l` &&
        test $count -eq 0
'

test_expect_success HAVE_JQ 'job-list stats indicates 0 inactive jobs' '
        count=`flux module stats --parse "jobs.inactive" job-list` &&
        test ${count} -eq 0 &&
        count=`get_completed_count` &&
        test ${count} -eq 0 &&
        count=`get_failed_count` &&
        test ${count} -eq 0
'

#
# Bad requests
#
test_expect_success 'job-list.purge with invalid count fails with EPROTO(71)' '
     $jq -j -c -n  "{count:-1}" | ${RPC} job-list.purge 71
'

test_done
