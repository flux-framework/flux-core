#!/bin/sh

test_description='Test flux job list services w/ changing job data'

. $(dirname $0)/job-list/job-list-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

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
#   - pending jobs - by priority (highest first), job id (smaller first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#
# TODO
# - alternate userid job listing

test_expect_success 'submit jobs for job list testing' '
        #  Create `hostname` and `sleep` jobspec
        #  N.B. Used w/ `flux job submit` for serial job submission
        #  for efficiency (vs serial `flux submit`.
        #
        flux submit --dry-run hostname >hostname.json &&
        flux submit --dry-run --time-limit=5m sleep 600 > sleeplong.json &&
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
        #  Submit 8 sleep jobs to fill up resources
        #
        for i in $(seq 0 7); do
                flux job submit sleeplong.json >> runningids
        done &&
        tac runningids | flux job id > running.ids &&
        #
        #  Submit a set of jobs with misc urgencies
        #
        id1=$(flux job submit -u20 hostname.json) &&
        id2=$(flux job submit      hostname.json) &&
        id3=$(flux job submit -u31 hostname.json) &&
        id4=$(flux job submit -u0  hostname.json) &&
        id5=$(flux job submit -u20 hostname.json) &&
        id6=$(flux job submit      hostname.json) &&
        id7=$(flux job submit -u31 hostname.json) &&
        id8=$(flux job submit -u0  hostname.json) &&
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
        #  The job-list module has eventual consistency with the jobs stored in
        #  the job-manager queue.  To ensure no raciness in tests, ensure
        #  jobs above have reached expected states in job-list before continuing.
        #
        flux job list-ids --wait-state=sched $(job_list_state_ids pending) > /dev/null &&
        flux job list-ids --wait-state=run $(job_list_state_ids running) > /dev/null &&
        flux job list-ids --wait-state=inactive $(job_list_state_ids inactive) > /dev/null
'

# Note: "running" = "run" | "cleanup", we also test just "run" state
# since we happen to know all these jobs are in the "run" state given
# checks above

test_expect_success 'flux job list running jobs in started order' '
        flux job list -s running | jq .id > list_started.out &&
        test_cmp list_started.out running.ids
'

test_expect_success 'flux job list inactive jobs in completed order' '
        flux job list -s inactive | jq .id > list_inactive.out &&
        test_cmp list_inactive.out inactive.ids
'

# Note: "pending" = "depend" | "sched", we also test just "sched"
# state since we happen to know all these jobs are in the "sched"
# state given checks above

test_expect_success 'flux job list pending jobs in priority order' '
        flux job list -s pending | jq .id > list_pending.out &&
        test_cmp list_pending.out pending.ids
'

test_expect_success 'flux job list pending jobs with correct urgency' '
        cat >list_urgency.exp <<-EOT &&
31
31
20
20
16
16
0
0
EOT
        flux job list -s pending | jq .urgency > list_urgency.out &&
        test_cmp list_urgency.out list_urgency.exp
'

test_expect_success 'flux job list pending jobs with correct priority' '
        cat >list_priority.exp <<-EOT &&
4294967295
4294967295
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

test_expect_success 'change a job urgency' '
        jobid=`head -n 3 pending.ids | tail -n 1` &&
        flux job urgency ${jobid} 1
'

test_expect_success 'wait for job-list to see job urgency change' '
        jobidhead=`head -n 2 pending.ids > pending_after.ids` &&
        jobidhead=`head -n 6 pending.ids | tail -n 3 >> pending_after.ids` &&
        jobidchange=`head -n 3 pending.ids | tail -n 1 >> pending_after.ids` &&
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

test_expect_success 'flux job list pending jobs with correct urgency' '
        cat >list_urgency_after.exp <<-EOT &&
31
31
20
16
16
1
0
0
EOT
        flux job list -s pending | jq .urgency > list_urgency_after.out &&
        test_cmp list_urgency_after.out list_urgency_after.exp
'

test_expect_success 'flux job list pending jobs with correct priority' '
        cat >list_priority_after.exp <<-EOT &&
4294967295
4294967295
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
	flux cancel $(cat active.ids) &&
        for jobid in `cat active.ids`; do
            fj_wait_event $jobid clean;
        done
'

test_done
