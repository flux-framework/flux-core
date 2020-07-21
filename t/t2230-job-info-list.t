#!/bin/sh

test_description='Test flux job info list services'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc
listRPC="flux python ${SHARNESS_TEST_SRCDIR}/job-info/list-rpc.py"

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

wait_jobid_state() {
        local jobid=$1
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
        tac inactiveids > completed.ids &&
        #
        #  Run a job that will fail, copy its JOBID to both inactive and
        #   failed lists.
        #
        jobid=`flux mini submit nosuchcommand` &&
        fj_wait_event $jobid clean &&
        echo $jobid >> inactiveids &&
        echo $jobid > failed.ids &&
        #
        #  Submit 8 sleep jobs to fill up resources
        #
        for i in $(seq 0 7); do
                flux job submit sleeplong.json >> runningids
        done &&
        tac runningids > running.ids &&
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
        echo $id3 > pending.ids &&
        echo $id7 >> pending.ids &&
        echo $id1 >> pending.ids &&
        echo $id5 >> pending.ids &&
        echo $id2 >> pending.ids &&
        echo $id6 >> pending.ids &&
        echo $id4 >> pending.ids &&
        echo $id8 >> pending.ids &&
        cat pending.ids > active.ids &&
        cat running.ids >> active.ids &&
        #
        #  Submit a job and cancel it
        #
        jobid=`flux mini submit cancelledjob` &&
        flux job wait-event $jobid depend &&
        flux job cancel $jobid &&
        flux job wait-event $jobid clean &&
        echo $jobid >> inactiveids &&
        echo $jobid > cancelled.ids &&
        tac inactiveids > inactive.ids &&
        cat active.ids > all.ids &&
        cat inactive.ids >> all.ids &&
        #
        #  Synchronize all expected states
        #
        wait_states
'

# Note: "running" = "run" & "cleanup", we also test just "run" state
# since we happen to know all these jobs are in the "run" state given
# checks above

test_expect_success HAVE_JQ 'flux job list running jobs in started order' '
        flux job list -s running | jq .id > list_started1.out &&
        flux job list -s run,cleanup | jq .id > list_started2.out &&
        flux job list -s run | jq .id > list_started3.out &&
        test_cmp list_started1.out running.ids &&
        test_cmp list_started2.out running.ids &&
        test_cmp list_started3.out running.ids
'

test_expect_success HAVE_JQ 'flux job list running jobs with correct state' '
        for count in `seq 1 8`; do \
            echo "8" >> list_state_R.exp; \
        done &&
        flux job list -s running | jq .state > list_state_R1.out &&
        flux job list -s run,cleanup | jq .state > list_state_R2.out &&
        flux job list -s run | jq .state > list_state_R3.out &&
        test_cmp list_state_R1.out list_state_R.exp &&
        test_cmp list_state_R2.out list_state_R.exp &&
        test_cmp list_state_R3.out list_state_R.exp
'

test_expect_success HAVE_JQ 'flux job list no jobs in cleanup state' '
        count=$(flux job list -s cleanup | wc -l) &&
        test $count -eq 0
'

test_expect_success HAVE_JQ 'flux job list inactive jobs in completed order' '
        flux job list -s inactive | jq .id > list_inactive.out &&
        test_cmp list_inactive.out inactive.ids
'

test_expect_success HAVE_JQ 'flux job list inactive jobs with correct state' '
        for count in `seq 1 6`; do \
            echo "32" >> list_state_I.exp; \
        done &&
        flux job list -s inactive | jq .state > list_state_I.out &&
        test_cmp list_state_I.out list_state_I.exp
'

test_expect_success HAVE_JQ 'flux job list inactive jobs results are correct' '
        flux job list -s inactive | jq .result > list_result_I.out &&
        echo "4" >> list_result_I.exp &&
        echo "2" >> list_result_I.exp &&
        for count in `seq 1 4`; do \
            echo "1" >> list_result_I.exp; \
        done &&
        test_cmp list_result_I.out list_result_I.exp
'

# Hard code state/results values for these tests, as we did not add a results
# option to flux_job_list() or the flux-job command.

test_expect_success HAVE_JQ 'flux job list only cancelled jobs' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:1000, userid:${id}, states:32, results:4, attrs:[]}" \
          | $RPC job-info.list | $jq .jobs | $jq -c '.[]' | $jq .id > list_result_cancelled.out &&
        test_cmp cancelled.ids list_result_cancelled.out
'

test_expect_success HAVE_JQ 'flux job list only failed jobs' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:1000, userid:${id}, states:32, results:2, attrs:[]}" \
          | $RPC job-info.list | $jq .jobs | $jq -c '.[]' | $jq .id > list_result_failed.out &&
        test_cmp failed.ids list_result_failed.out
'

test_expect_success HAVE_JQ 'flux job list only completed jobs' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:1000, userid:${id}, states:32, results:1, attrs:[]}" \
          | $RPC job-info.list | $jq .jobs | $jq -c '.[]' | $jq .id > list_result_completed.out &&
        test_cmp completed.ids list_result_completed.out
'

# Note: "pending" = "depend" & "sched", we also test just "sched"
# state since we happen to know all these jobs are in the "sched"
# state given checks above

test_expect_success HAVE_JQ 'flux job list pending jobs in priority order' '
        flux job list -s pending | jq .id > list_pending1.out &&
        flux job list -s depend,sched | jq .id > list_pending2.out &&
        flux job list -s sched | jq .id > list_pending3.out &&
        test_cmp list_pending1.out pending.ids &&
        test_cmp list_pending2.out pending.ids &&
        test_cmp list_pending3.out pending.ids
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
        flux job list -s pending | jq .priority > list_priority1.out &&
        flux job list -s depend,sched | jq .priority > list_priority2.out &&
        flux job list -s sched | jq .priority > list_priority3.out &&
        test_cmp list_priority1.out list_priority.exp &&
        test_cmp list_priority2.out list_priority.exp &&
        test_cmp list_priority3.out list_priority.exp
'

test_expect_success HAVE_JQ 'flux job list pending jobs with correct state' '
        for count in `seq 1 8`; do \
            echo "4" >> list_state_S.exp; \
        done &&
        flux job list -s sched | jq .state > list_state_S.out &&
        test_cmp list_state_S.out list_state_S.exp
'

test_expect_success HAVE_JQ 'flux job list no jobs in depend state' '
        count=$(flux job list -s depend | wc -l) &&
        test $count -eq 0
'

# Note: "active" = "pending" & "running", i.e. depend, sched, run, cleanup
test_expect_success HAVE_JQ 'flux job list active jobs in correct order' '
        flux job list -s active | jq .id > list_active1.out &&
        flux job list -s depend,sched,run,cleanup | jq .id > list_active2.out &&
        flux job list -s sched,run | jq .id > list_active3.out &&
        test_cmp list_active1.out active.ids &&
        test_cmp list_active2.out active.ids &&
        test_cmp list_active3.out active.ids
'

test_expect_success HAVE_JQ 'flux job list jobs with correct userid' '
        for count in `seq 1 $(state_count all)`; do \
            id -u >> list_userid.exp; \
        done &&
        flux job list -a | jq .userid > list_userid.out &&
        test_cmp list_userid.out list_userid.exp
'

test_expect_success HAVE_JQ 'flux job list defaults to listing pending & running jobs' '
        flux job list | jq .id > list_default.out &&
        count=$(wc -l < list_default.out) &&
        test $count = $(state_count active) &&
        test_cmp list_default.out active.ids
'

test_expect_success 'flux job list --user=userid works' '
        uid=$(id -u) &&
        flux job list --user=$uid> list_userid.out &&
        count=$(wc -l < list_userid.out) &&
        test $count = $(state_count active)
'

test_expect_success 'flux job list --user=all works' '
        flux job list --user=all > list_all.out &&
        count=$(wc -l < list_all.out) &&
        test $count = $(state_count active)
'

# we hard count numbers here b/c its a --count test
test_expect_success HAVE_JQ 'flux job list --count works' '
        flux job list -s active,inactive --count=12 | jq .id > list_count.out &&
        count=$(wc -l < list_count.out) &&
        test "$count" = "12" &&
        cat pending.ids > list_count.exp &&
        head -n 4 running.ids >> list_count.exp &&
        test_cmp list_count.out list_count.exp
'

test_expect_success HAVE_JQ 'flux job list all jobs works' '
        flux job list -a | jq .id > list_all_jobids.out &&
        test_cmp all.ids list_all_jobids.out
'

test_expect_success HAVE_JQ 'job stats lists jobs in correct state (mix)' '
        flux job stats | jq -e ".job_states.depend == 0" &&
        flux job stats | jq -e ".job_states.sched == $(state_count pending)" &&
        flux job stats | jq -e ".job_states.run == $(state_count running)" &&
        flux job stats | jq -e ".job_states.cleanup == 0" &&
        flux job stats | jq -e ".job_states.inactive == $(state_count inactive)" &&
        flux job stats | jq -e ".job_states.total == $(state_count all)"
'

test_expect_success 'cleanup job listing jobs ' '
        for jobid in `cat active.ids`; do \
            flux job cancel $jobid; \
            fj_wait_event $jobid clean; \
        done
'

wait_inactive() {
        local i=0
        while [ "$(flux job list --states=inactive | wc -l)" != "$(state_count all)" ] \
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

test_expect_success 'reload the job-info module' '
        flux job list -a > before_reload.out &&
        flux module reload job-info &&
        wait_inactive
'

test_expect_success HAVE_JQ 'job-info: list successfully reconstructed' '
        flux job list -a > after_reload.out &&
        test_cmp before_reload.out after_reload.out
'

test_expect_success HAVE_JQ 'job stats lists jobs in correct state (all inactive)' '
        flux job stats | jq -e ".job_states.depend == 0" &&
        flux job stats | jq -e ".job_states.sched == 0" &&
        flux job stats | jq -e ".job_states.run == 0" &&
        flux job stats | jq -e ".job_states.cleanup == 0" &&
        flux job stats | jq -e ".job_states.inactive == $(state_count all)" &&
        flux job stats | jq -e ".job_states.total == $(state_count all)"
'

# job list-inactive

test_expect_success HAVE_JQ 'flux job list-inactive lists all inactive jobs' '
        flux job list-inactive > list-inactive.out &&
        count=`cat list-inactive.out | wc -l` &&
        test $count -eq $(state_count all)
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since 0 lists all inactive jobs' '
        count=`flux job list-inactive --since=0 | wc -l` &&
        test $count -eq $(state_count all)
'

# we hard count numbers here b/c its a --count test
test_expect_success HAVE_JQ 'flux job list-inactive w/ count limits output of inactive jobs' '
        count=`flux job list-inactive --count=14 | wc -l` &&
        test $count -eq 14
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since -1 leads to error' '
        test_must_fail flux job list-inactive --since=-1
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ count -1 leads to error' '
        test_must_fail flux job list-inactive --count=-1
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since (most recent timestamp)' '
        timestamp=`cat list-inactive.out | head -n 1 | jq .t_inactive` &&
        count=`flux job list-inactive --since=${timestamp} | wc -l` &&
        test $count -eq 0
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since (second to most recent timestamp)' '
        timestamp=`cat list-inactive.out | head -n 2 | tail -n 1 | jq .t_inactive` &&
        count=`flux job list-inactive --since=${timestamp} | wc -l` &&
        test $count -eq 1
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since (oldest timestamp)' '
        timestamp=`cat list-inactive.out | tail -n 1 | jq .t_inactive` &&
        count=`flux job list-inactive --since=${timestamp} | wc -l` &&
        test $count -eq 21
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since (middle timestamp #1)' '
        timestamp=`cat list-inactive.out | head -n 8 | tail -n 1 | jq .t_inactive` &&
        count=`flux job list-inactive --since=${timestamp} | wc -l` &&
        test $count -eq 7
'

test_expect_success HAVE_JQ 'flux job list-inactive w/ since (middle timestamp #2)' '
        timestamp=`cat list-inactive.out | head -n 13 | tail -n 1 | jq .t_inactive` &&
        count=`flux job list-inactive --since=${timestamp} | wc -l` &&
        test $count -eq 12
'


# job list-id

test_expect_success HAVE_JQ 'flux job list-ids works with a single ID' '
        id=`head -n 1 pending.ids` &&
        flux job list-ids $id | jq -e ".id == ${id}" &&
        id=`head -n 1 running.ids` &&
        flux job list-ids $id | jq -e ".id == ${id}" &&
        id=`head -n 1 inactive.ids` &&
        flux job list-ids $id | jq -e ".id == ${id}"
'

test_expect_success HAVE_JQ 'flux job list-ids multiple IDs works' '
        ids=$(state_ids pending) &&
        flux job list-ids $ids | jq .id > list_idsP.out &&
        test_cmp list_idsP.out pending.ids &&
        ids=$(state_ids running) &&
        flux job list-ids $ids | jq .id > list_idsR.out &&
        test_cmp list_idsR.out running.ids &&
        ids=$(state_ids inactive) &&
        flux job list-ids $ids | jq .id > list_idsI.out &&
        test_cmp list_idsI.out inactive.ids &&
        ids=$(state_ids all) &&
        flux job list-ids $ids | jq .id > list_idsPRI.out &&
        cat pending.ids running.ids inactive.ids > list_idsPRI.exp &&
        test_cmp list_idsPRI.exp list_idsPRI.out
'

test_expect_success HAVE_JQ 'flux job list-ids fails without ID' '
        test_must_fail flux job list-ids
'

test_expect_success HAVE_JQ 'flux job list-ids fails with bad ID' '
        test_must_fail flux job list-ids 1234567890
'

test_expect_success HAVE_JQ 'flux job list-ids fails with not an ID' '
        test_must_fail flux job list-ids foobar
'

test_expect_success HAVE_JQ 'flux job list-ids fails with one bad ID out of several' '
        id1=`head -n 1 pending.ids` &&
        id2=`head -n 1 running.ids` &&
        id3=`head -n 1 inactive.ids` &&
        test_must_fail flux job list-ids ${id1} ${id2} 1234567890 ${id3}
'

# In order to test potential racy behavior, use job state pause/unpause to pause
# the handling of job state transitions from the job-manager.
#
# Note that between the background process of `flux job list-ids` and
# `unpause`, we must ensure the background process has sent the
# request to the job-info service and is now waiting for the id to be
# synced.  We call wait_idsync to check stats for this to ensure the
# racy behavior is covered.

wait_idsync() {
        local num=$1
        local i=0
        while (! flux module stats --parse idsync.waits job-info > /dev/null 2>&1 \
               || [ "$(flux module stats --parse idsync.waits job-info 2> /dev/null)" != "$num" ]) \
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

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'flux job list-ids waits for job ids (one id)' '
	${RPC} job-info.job-state-pause 0 </dev/null
        jobid=`flux mini submit hostname`
        fj_wait_event $jobid clean >/dev/null
        flux job list-ids ${jobid} > list_id_wait1.out &
        pid=$!
        wait_idsync 1 &&
	${RPC} job-info.job-state-unpause 0 </dev/null &&
        wait $pid &&
        cat list_id_wait1.out | jq -e ".id == ${jobid}"
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'flux job list-ids waits for job ids (different ids)' '
	${RPC} job-info.job-state-pause 0 </dev/null
        jobid1=`flux mini submit hostname`
        jobid2=`flux mini submit hostname`
        fj_wait_event ${jobid1} clean >/dev/null
        fj_wait_event ${jobid2} clean >/dev/null
        flux job list-ids ${jobid1} ${jobid2} > list_id_wait2.out &
        pid=$!
        wait_idsync 2 &&
	${RPC} job-info.job-state-unpause 0 </dev/null &&
        wait $pid &&
        grep ${jobid1} list_id_wait2.out &&
        grep ${jobid2} list_id_wait2.out
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'flux job list-ids waits for job ids (same id)' '
	${RPC} job-info.job-state-pause 0 </dev/null
        jobid=`flux mini submit hostname`
        fj_wait_event $jobid clean >/dev/null
        flux job list-ids ${jobid} > list_id_wait3A.out &
        pid1=$!
        flux job list-ids ${jobid} > list_id_wait3B.out &
        pid2=$!
        wait_idsync 1 &&
	${RPC} job-info.job-state-unpause 0 </dev/null &&
        wait ${pid1} &&
        wait ${pid2} &&
        cat list_id_wait3A.out | jq -e ".id == ${jobid}" &&
        cat list_id_wait3B.out | jq -e ".id == ${jobid}"
'

#
# job list timing
#

# simply test that value in timestamp increases through job states
test_expect_success HAVE_JQ 'flux job list job state timing outputs valid (job inactive)' '
        jobid=$(flux mini submit hostname) &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".t_depend < .t_sched" &&
        echo $obj | jq -e ".t_sched < .t_run" &&
        echo $obj | jq -e ".t_run < .t_cleanup" &&
        echo $obj | jq -e ".t_cleanup < .t_inactive"
'

# since job is running, make sure latter states don't exist
test_expect_success HAVE_JQ 'flux job list job state timing outputs valid (job running)' '
        jobid=$(flux mini submit sleep 60) &&
        fj_wait_event $jobid start >/dev/null &&
        wait_jobid_state $jobid running &&
        obj=$(flux job list -s running | grep $jobid) &&
        echo $obj | jq -e ".t_depend < .t_sched" &&
        echo $obj | jq -e ".t_sched < .t_run" &&
        echo $obj | jq -e ".t_cleanup == null" &&
        echo $obj | jq -e ".t_inactive == null" &&
        flux job cancel $jobid &&
        fj_wait_event $jobid clean >/dev/null
'

#
# job names
#

test_expect_success HAVE_JQ 'flux job list outputs user job name' '
        jobid=`flux mini submit --setattr system.job.name=foobar A B C` &&
        echo $jobid > jobname1.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        flux job list -s inactive | grep $jobid | jq -e ".name == \"foobar\""
'

test_expect_success HAVE_JQ 'flux job lists first argument for job name' '
        jobid=`flux mini submit mycmd arg1 arg2` &&
        echo $jobid > jobname2.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        flux job list -s inactive | grep $jobid | jq -e ".name == \"mycmd\""
'

test_expect_success HAVE_JQ 'flux job lists basename of first argument for job name' '
        jobid=`flux mini submit /foo/bar arg1 arg2` &&
        echo $jobid > jobname3.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        flux job list -s inactive | grep $jobid | jq -e ".name == \"bar\""
'

test_expect_success HAVE_JQ 'flux job lists full path for job name if basename fails on first arg' '
        jobid=`flux mini submit /foo/bar/ arg1 arg2` &&
        echo $jobid > jobname4.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        flux job list -s inactive | grep $jobid | jq -e ".name == \"\/foo\/bar\/\""
'

test_expect_success 'reload the job-info module' '
        flux module reload job-info
'

test_expect_success HAVE_JQ 'verify job names preserved across restart' '
        jobid1=`cat jobname1.id` &&
        jobid2=`cat jobname2.id` &&
        jobid3=`cat jobname3.id` &&
        jobid4=`cat jobname4.id` &&
        flux job list -s inactive | grep ${jobid1} | jq -e ".name == \"foobar\"" &&
        flux job list -s inactive | grep ${jobid2} | jq -e ".name == \"mycmd\"" &&
        flux job list -s inactive | grep ${jobid3} | jq -e ".name == \"bar\"" &&
        flux job list -s inactive | grep ${jobid4} | jq -e ".name == \"\/foo\/bar\/\""
'

#
# job task count
#

test_expect_success HAVE_JQ 'flux job list outputs ntasks correctly (1 task)' '
        jobid=`flux mini submit hostname` &&
        echo $jobid > taskcount1.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".ntasks == 1"
'

test_expect_success HAVE_JQ 'flux job list outputs ntasks correctly (4 tasks)' '
        jobid=`flux mini submit -n4 hostname` &&
        echo $jobid > taskcount2.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".ntasks == 4"
'

test_expect_success 'reload the job-info module' '
        flux module reload job-info
'

test_expect_success HAVE_JQ 'verify task count preserved across restart' '
        jobid1=`cat taskcount1.id` &&
        jobid2=`cat taskcount2.id` &&
        obj=$(flux job list -s inactive | grep ${jobid1}) &&
        echo $obj | jq -e ".ntasks == 1" &&
        obj=$(flux job list -s inactive | grep ${jobid2}) &&
        echo $obj | jq -e ".ntasks == 4"
'

#
# job node count / rank list
#

test_expect_success HAVE_JQ 'flux job list outputs nnodes/ranks correctly (1 task / 1 node)' '
        jobid=`flux mini submit -n1 hostname` &&
        echo $jobid > nodecount1.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".nnodes == 1" &&
        echo $obj | jq -e ".ranks == \"0\""
'

test_expect_success HAVE_JQ 'flux job list outputs nnodes/ranks correctly (2 tasks, / 1 node)' '
        jobid=`flux mini submit -n2 hostname` &&
        echo $jobid > nodecount2.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".nnodes == 1" &&
        echo $obj | jq -e ".ranks == \"0\""
'

test_expect_success HAVE_JQ 'flux job list outputs nnodes/ranks correctly (3 tasks, / 2 nodes)' '
        jobid=`flux mini submit -n3 hostname` &&
        echo $jobid > nodecount3.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".nnodes == 2" &&
        echo $obj | jq -e ".ranks == \"[0-1]\""
'

test_expect_success HAVE_JQ 'flux job list outputs nnodes/ranks correctly (5 tasks, / 3 nodes)' '
        jobid=`flux mini submit -n5 hostname` &&
        echo $jobid > nodecount4.id &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".nnodes == 3" &&
        echo $obj | jq -e ".ranks == \"[0-2]\""
'

#
# job success
#

test_expect_success HAVE_JQ 'flux job list outputs success correctly (true)' '
        jobid=`flux mini submit hostname` &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".success == true"
'

test_expect_success HAVE_JQ 'flux job list outputs success correctly (false)' '
        jobid=`flux mini submit nosuchcommand` &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".success == false"
'

# job exceptions

test_expect_success HAVE_JQ 'flux job list outputs exceptions correctly (no exception)' '
        jobid=`flux mini submit hostname` &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".exception_occurred == false" &&
        echo $obj | jq -e ".exception_severity == null" &&
        echo $obj | jq -e ".exception_type == null" &&
        echo $obj | jq -e ".exception_note == null"
'

test_expect_success HAVE_JQ 'flux job list outputs exceptions correctly (exception)' '
        jobid=`flux mini submit nosuchcommand` &&
        fj_wait_event $jobid clean >/dev/null &&
        wait_jobid_state $jobid inactive &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".exception_occurred == true" &&
        echo $obj | jq -e ".exception_severity == 0" &&
        echo $obj | jq -e ".exception_type == \"exec\"" &&
        echo $obj | jq .exception_note | grep "No such file or directory"
'

# expiration time

test_expect_success HAVE_JQ 'flux job list outputs expiration time when set' '
	jobid=$(flux mini submit -t 30 sleep 1000) &&
	fj_wait_event $jobid start &&
	flux job list | grep $jobid > expiration.json &&
	test_debug "cat expiration.json" &&
	jq -e ".expiration > now" < expiration.json &&
	flux job cancel $jobid
'

test_expect_success 'reload the job-info module' '
        flux module reload job-info
'

test_expect_success HAVE_JQ 'verify nnodes preserved across restart' '
        jobid1=`cat nodecount1.id` &&
        jobid2=`cat nodecount2.id` &&
        jobid3=`cat nodecount3.id` &&
        jobid4=`cat nodecount4.id` &&
        obj=$(flux job list -s inactive | grep ${jobid1}) &&
        echo $obj | jq -e ".nnodes == 1" &&
        echo $obj | jq -e ".ranks == \"0\"" &&
        obj=$(flux job list -s inactive | grep ${jobid2}) &&
        echo $obj | jq -e ".nnodes == 1" &&
        echo $obj | jq -e ".ranks == \"0\"" &&
        obj=$(flux job list -s inactive | grep ${jobid3}) &&
        echo $obj | jq -e ".nnodes == 2" &&
        echo $obj | jq -e ".ranks == \"[0-1]\"" &&
        obj=$(flux job list -s inactive | grep ${jobid4}) &&
        echo $obj | jq -e ".nnodes == 3" &&
        echo $obj | jq -e ".ranks == \"[0-2]\""
'

#
# job list special cases
#

test_expect_success 'list count / max_entries works' '
        count=`flux job list -s inactive -c 0 | wc -l` &&
        test $count -gt 5 &&
        count=`flux job list -s inactive -c 5 | wc -l` &&
        test $count -eq 5
'

# List of all attributes (XXX: maybe this should be pulled in from somewhere
#  else? E.g. documentation?

ALL_ATTRIBUTES="\
userid \
priority \
t_submit \
t_depend \
t_sched \
t_run \
t_cleanup \
t_inactive \
state \
name \
ntasks \
nnodes \
ranks \
success \
exception_occurred \
exception_type \
exception_severity \
exception_note \
result \
expiration
"

test_expect_success HAVE_JQ 'list request with empty attrs works' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, states:0, results:0, attrs:[]}" \
          | $RPC job-info.list > list_empty_attrs.out &&
	for attr in $ALL_ATTRIBUTES; do
	    test_must_fail grep $attr list_empty_attrs.out
	done
'
test_expect_success HAVE_JQ 'list request with excessive max_entries works' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:100000, userid:${id}, states:0, results:0, attrs:[]}" \
          | $RPC job-info.list
'
test_expect_success HAVE_JQ 'list-attrs works' '
        $RPC job-info.list-attrs < /dev/null > list_attrs.out &&
	for attr in $ALL_ATTRIBUTES; do
	    grep $attr list_attrs.out
	done
'

#
#
# stats & corner cases
#

test_expect_success 'job-info stats works' '
        flux module stats --parse jobs.pending job-info &&
        flux module stats --parse jobs.running job-info &&
        flux module stats --parse jobs.inactive job-info &&
        flux module stats --parse idsync.lookups job-info &&
        flux module stats --parse idsync.waits job-info
'
test_expect_success 'list request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.list 71 </dev/null
'
test_expect_success HAVE_JQ 'list request with invalid input fails with EPROTO(71) (attrs not an array)' '
	name="attrs-not-array" &&
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, states:0, results:0, attrs:5}" \
          | $listRPC >${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 71: invalid payload: attrs must be an array
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list request with invalid input fails with EINVAL(22) (attrs non-string)' '
	name="attr-not-string" &&
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, states:0, results:0, attrs:[5]}" \
	  | $listRPC > ${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 22: attr has no string value
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list request with invalid input fails with EINVAL(22) (attrs illegal field)' '
	name="field-not-valid" &&
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, states:0, results:0, attrs:[\"foo\"]}" \
          | $listRPC > ${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 22: foo is not a valid attribute
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'list-id request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.list-id 71 </dev/null
'
test_expect_success HAVE_JQ 'list-id request with invalid input fails with EPROTO(71) (attrs not an array)' '
	name="list-id-attrs-not-array" &&
        id=`flux mini submit hostname` &&
        $jq -j -c -n  "{id:${id}, attrs:5}" \
          | $listRPC list-id > ${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 71: invalid payload: attrs must be an array
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list-id request with invalid input fails with EINVAL(22) (attrs non-string)' '
	name="list-id-invalid-attrs" &&
	id=$(flux jobs -c1 -ano {id}) &&
        $jq -j -c -n  "{id:${id}, attrs:[5]}" \
          | $listRPC list-id > ${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 22: attr has no string value
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list-id request with invalid input fails with EINVAL(22) (attrs illegal field)' '
	name="list-id-invalid-attr" &&
	id=$(flux jobs -c1 -ano {id}) &&
        $jq -j -c -n  "{id:${id}, attrs:[\"foo\"]}" \
          | $listRPC list-id >${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 22: foo is not a valid attribute
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'list-inactive request with empty payload fails with EPROTO(71)' '
	name="list-inactive-empty" &&
	${listRPC} list-inactive </dev/null >${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 71: invalid payload: '\''['\'' or '\''{'\'' expected near end of file
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list-inactive request with invalid input fails with EPROTO(71) (attrs not an array)' '
	name="list-inactive-invalid" &&
        $jq -j -c -n  "{max_entries:5, since:0.0, attrs:5}" \
          | $listRPC list-inactive > ${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 71: invalid payload: attrs must be an array
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list-inactive request with invalid input fails with EINVAL(22) (attrs non-string)' '
	name="list-inactive-attrs-invalid" &&
        $jq -j -c -n  "{max_entries:5, since:0.0, attrs:[5]}" \
          | $listRPC list-inactive >${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 22: attr has no string value
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success HAVE_JQ 'list-inactive request with invalid input fails with EINVAL(22) (attrs illegal field)' '
        $jq -j -c -n  "{max_entries:5, since:0.0, attrs:[\"foo\"]}" \
          | $listRPC list-inactive >${name}.out &&
	cat <<-EOF >${name}.expected &&
	errno 22: foo is not a valid attribute
	EOF
	test_cmp ${name}.expected ${name}.out
'

#
# stress test
#

wait_jobs_finish() {
        local i=0
        while ([ "$(flux job list | wc -l)" != "0" ]) \
              && [ $i -lt 1000 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "1000" ]
        then
            return 1
        fi
        return 0
}

test_expect_success LONGTEST 'stress job-info.list-id' '
        flux python ${FLUX_SOURCE_DIR}/t/job-info/list-id.py 500 &&
        wait_jobs_finish
'
test_done
