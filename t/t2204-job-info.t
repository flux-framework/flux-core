#!/bin/sh

test_description='Test flux job info service'

. `dirname $0`/kvs/kvs-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ
RPC=${FLUX_BUILD_DIR}/t/request/rpc

# Usage: submit_job
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.
submit_job() {
        local jobid=$(flux job submit sleeplong.json) &&
        flux job wait-event $jobid start >/dev/null &&
        flux job cancel $jobid &&
        flux job wait-event $jobid clean >/dev/null &&
        echo $jobid
}

# Unlike above, do not cancel the job, the test will cancel the job
submit_job_live() {
        local jobspec=$1
        local jobid=$(flux job submit $jobspec) &&
        flux job wait-event $jobid start >/dev/null &&
        echo $jobid
}

# Test will cancel the job, is assumed won't run immediately
submit_job_wait() {
        local jobid=$(flux job submit sleeplong.json) &&
        flux job wait-event $jobid depend >/dev/null &&
        echo $jobid
}

wait_watchers_nonzero() {
        local str=$1
        local i=0
        while (! flux module stats --parse $str job-info > /dev/null 2>&1 \
               || [ "$(flux module stats --parse $str job-info 2> /dev/null)" = "0" ]) \
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

get_timestamp_field() {
        local field=$1
        local file=$2
        grep $field $file | awk '{print $1}'
}

test_expect_success 'job-info: generate jobspec for simple test job' '
        flux jobspec --format json srun -N1 hostname > hostname.json &&
        flux jobspec --format json srun -N1 sleep 300 > sleeplong.json
'

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'load job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux exec -r all flux module load barrier &&
        flux module load sched-simple &&
        flux module load job-exec
'

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
#   - pending jobs - by priority (highest first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#
# TODO
# - in order to test sorting, jobs should be submitted below in an
#   unordered fashion.  However, issues exist that do not allow for
#   such testing at this moment.  See Issue #2470.
# - alternate userid job listing

test_expect_success 'submit jobs for job list testing' '
        for i in `seq 1 4`; do \
            flux job submit hostname.json >> job_ids1.out; \
            flux job wait-event `tail -n 1 job_ids1.out` clean ; \
        done &&
        tac job_ids1.out > job_ids_inactive.out &&
        for i in `seq 1 8`; do \
            flux job submit sleeplong.json >> job_ids2.out; \
            flux job wait-event `tail -n 1 job_ids2.out` start; \
        done &&
        tac job_ids2.out > job_ids_running.out &&
        id1=$(flux job submit -p31 hostname.json) &&
        id2=$(flux job submit -p20 hostname.json) &&
        id3=$(flux job submit      hostname.json) &&
        id4=$(flux job submit -p0  hostname.json) &&
        echo $id1 >> job_ids_pending.out &&
        echo $id2 >> job_ids_pending.out &&
        echo $id3 >> job_ids_pending.out &&
        echo $id4 >> job_ids_pending.out
'

#
# the job-info module has eventual consistency with the jobs stored in
# the job-manager's queue.  We can't be 100% sure that the pending
# jobs have been updated in the job-info module by the time these
# tests have started.  To reduce the chances of that happening, all
# tests related to pending jobs are done after the running / inactive
# tests.
#

test_expect_success HAVE_JQ 'flux job list running jobs in started order' '
        flux job list -s running | jq .id > list_started.out &&
        test_cmp list_started.out job_ids_running.out
'

test_expect_success HAVE_JQ 'flux job list running jobs with correct state' '
        for count in `seq 1 8`; do \
            echo "8" >> list_state_R.exp; \
        done &&
        flux job list -s running | jq .state > list_state_R.out &&
        test_cmp list_state_R.out list_state_R.exp
'

test_expect_success HAVE_JQ 'flux job list inactive jobs in completed order' '
        flux job list -s inactive | jq .id > list_inactive.out &&
        test_cmp list_inactive.out job_ids_inactive.out
'

test_expect_success HAVE_JQ 'flux job list inactive jobs with correct state' '
        for count in `seq 1 4`; do \
            echo "32" >> list_state_I.exp; \
        done &&
        flux job list -s inactive | jq .state > list_state_I.out &&
        test_cmp list_state_I.out list_state_I.exp
'

test_expect_success HAVE_JQ 'flux job list pending jobs in priority order' '
        flux job list -s pending | jq .id > list_pending.out &&
        test_cmp list_pending.out job_ids_pending.out
'

test_expect_success HAVE_JQ 'flux job list pending jobs with correct priority' '
        cat >list_priority.exp <<-EOT &&
31
20
16
0
EOT
        flux job list -s pending | jq .priority > list_priority.out &&
        test_cmp list_priority.out list_priority.exp
'

test_expect_success HAVE_JQ 'flux job list pending jobs with correct state' '
        for count in `seq 1 4`; do \
            echo "4" >> list_state_S.exp; \
        done &&
        flux job list -s pending | jq .state > list_state_S.out &&
        test_cmp list_state_S.out list_state_S.exp
'

test_expect_success HAVE_JQ 'flux job list jobs with correct userid' '
        for count in `seq 1 16`; do \
            id -u >> list_userid.exp; \
        done &&
        flux job list -a | jq .userid > list_userid.out &&
        test_cmp list_userid.out list_userid.exp
'

test_expect_success HAVE_JQ 'flux job list defaults to listing pending & running jobs' '
        flux job list > list_default.out &&
        count=$(wc -l < list_default.out) &&
        test "$count" = "12" &&
        tail -n 8 list_default.out | jq .id  > list_default_running.out &&
        test_cmp list_default_running.out job_ids_running.out &&
        head -n 4 list_default.out | jq .id > list_default_pending.out &&
        test_cmp list_default_pending.out job_ids_pending.out
'

test_expect_success 'flux job list --userid=userid works' '
        uid=$(id -u) &&
        flux job list --userid=$uid> list_userid.out &&
        count=$(wc -l < list_userid.out) &&
        test "$count" = "12"
'

test_expect_success 'flux job list --userid=all works' '
        flux job list --userid=all > list_all.out &&
        count=$(wc -l < list_all.out) &&
        test "$count" = "12"
'

test_expect_success HAVE_JQ 'flux job list --count works' '
        flux job list -s active,inactive --count=8 > list_count.out &&
        count=$(wc -l < list_count.out) &&
        test "$count" = "8" &&
        head -n 4 list_count.out | jq .id > list_count_pending.out &&
        test_cmp list_count_pending.out job_ids_pending.out &&
        tail -n 4 list_count.out | jq .id > list_count_running.out &&
        head -n 4 job_ids_running.out > job_ids_running_head4.out &&
        test_cmp list_count_running.out job_ids_running_head4.out
'

test_expect_success HAVE_JQ 'flux job list all jobs works' '
        flux job list -a > list_all.out &&
        cat list_all.out | jq .id > list_all_jobids.out &&
        cat job_ids_pending.out >> list_all_jobids.exp &&
        cat job_ids_running.out >> list_all_jobids.exp &&
        cat job_ids_inactive.out >> list_all_jobids.exp &&
        test_cmp list_all_jobids.exp list_all_jobids.out
'

test_expect_success HAVE_JQ 'job stats lists jobs in correct state (mix)' '
        flux job stats | jq -e ".job_states.depend == 0" &&
        flux job stats | jq -e ".job_states.sched == 4" &&
        flux job stats | jq -e ".job_states.run == 8" &&
        flux job stats | jq -e ".job_states.cleanup == 0" &&
        flux job stats | jq -e ".job_states.inactive == 4" &&
        flux job stats | jq -e ".job_states.total == 16"
'

test_expect_success 'cleanup job listing jobs ' '
        for jobid in `cat job_ids_pending.out`; do \
            flux job cancel $jobid; \
            flux job wait-event $jobid clean; \
        done &&
        for jobid in `cat job_ids_running.out`; do \
            flux job cancel $jobid; \
            flux job wait-event $jobid clean; \
        done
'

test_expect_success 'reload the job-info module' '
        flux exec -r all flux module remove job-info &&
        flux exec -r all flux module load job-info
'

# inactive jobs are listed by most recently completed first, so must
# construct order based on order of jobs canceled above
test_expect_success HAVE_JQ 'job-info: list successfully reconstructed' '
        flux job list -a > list_reload.out &&
        for count in `seq 1 16`; do \
            echo "32" >> list_reload_state.exp; \
        done &&
        cat list_reload.out | jq .state  > list_reload_state.out &&
        test_cmp list_reload_state.exp list_reload_state.out &&
        tac job_ids_running.out >> list_reload_ids.exp &&
        tac job_ids_pending.out >> list_reload_ids.exp &&
        cat job_ids_inactive.out >> list_reload_ids.exp &&
        cat list_reload.out | jq .id > list_reload_ids.out &&
        test_cmp list_reload_ids.exp list_reload_ids.out
'

test_expect_success HAVE_JQ 'job stats lists jobs in correct state (all inactive)' '
        flux job stats | jq -e ".job_states.depend == 0" &&
        flux job stats | jq -e ".job_states.sched == 0" &&
        flux job stats | jq -e ".job_states.run == 0" &&
        flux job stats | jq -e ".job_states.cleanup == 0" &&
        flux job stats | jq -e ".job_states.inactive == 16" &&
        flux job stats | jq -e ".job_states.total == 16"
'

#
# job list timing
#

# simply test that value in timestamp increases through job states
test_expect_success HAVE_JQ 'flux job list job state timing outputs valid (job inactive)' '
        jobid=$(flux mini submit hostname) &&
        flux job wait-event $jobid clean >/dev/null &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".t_depend < .t_sched" &&
        echo $obj | jq ".t_sched < .t_run" &&
        echo $obj | jq ".t_run < .t_cleanup" &&
        echo $obj | jq ".t_cleanup < .t_inactive"
'

# since job is running, make sure latter states are 0.0000
test_expect_success HAVE_JQ 'flux job list job state timing outputs valid (job running)' '
        jobid=$(flux mini submit sleep 60) &&
        flux job wait-event $jobid start >/dev/null &&
        obj=$(flux job list -s running | grep $jobid) &&
        echo $obj | jq -e ".t_depend < .t_sched" &&
        echo $obj | jq -e ".t_sched < .t_run" &&
        echo $obj | jq -e ".t_cleanup == 0.0" &&
        echo $obj | jq -e ".t_inactive == 0.0" &&
        flux job cancel $jobid &&
        flux job wait-event $jobid clean >/dev/null
'

#
# job names
#

test_expect_success 'flux job list outputs user job name' '
        jobid=`flux mini submit --setattr system.job.name=foobar A B C` &&
        echo $jobid > jobname1.id &&
        flux job wait-event $jobid clean >/dev/null &&
        flux job list -s inactive | grep $jobid | grep foobar
'

test_expect_success 'flux job lists first argument for job name' '
        jobid=`flux mini submit mycmd arg1 arg2` &&
        echo $jobid > jobname2.id &&
        flux job wait-event $jobid clean >/dev/null &&
        flux job list -s inactive | grep $jobid | grep mycmd
'

test_expect_success 'flux job lists basename of first argument for job name' '
        jobid=`flux mini submit /foo/bar arg1 arg2` &&
        echo $jobid > jobname3.id &&
        flux job wait-event $jobid clean >/dev/null &&
        flux job list -s inactive | grep $jobid | grep bar &&
        flux job list -s inactive | grep $jobid | grep -v foo
'

test_expect_success 'flux job lists full path for job name if first argument not ok' '
        jobid=`flux mini submit /foo/bar/ arg1 arg2` &&
        echo $jobid > jobname4.id &&
        flux job wait-event $jobid clean >/dev/null &&
        flux job list -s inactive | grep $jobid | grep "\/foo\/bar\/"
'

test_expect_success 'reload the job-info module' '
        flux exec -r all flux module remove job-info &&
        flux exec -r all flux module load job-info
'

test_expect_success 'verify job names preserved across restart' '
        jobid1=`cat jobname1.id` &&
        jobid2=`cat jobname2.id` &&
        jobid3=`cat jobname3.id` &&
        jobid4=`cat jobname4.id` &&
        flux job list -s inactive | grep ${jobid1} | grep foobar &&
        flux job list -s inactive | grep ${jobid2} | grep mycmd &&
        flux job list -s inactive | grep ${jobid3} | grep bar &&
        flux job list -s inactive | grep ${jobid4} | grep "\/foo\/bar\/"
'

#
# job task count
#

test_expect_success 'flux job list outputs ntasks correctly (1 task)' '
        jobid=`flux mini submit hostname` &&
        echo $jobid > taskcount1.id &&
        flux job wait-event $jobid clean >/dev/null &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".[\"ntasks\"] == 1"
'

test_expect_success 'flux job list outputs ntasks correctly (4 tasks)' '
        jobid=`flux mini submit -n4 hostname` &&
        echo $jobid > taskcount2.id &&
        flux job wait-event $jobid clean >/dev/null &&
        obj=$(flux job list -s inactive | grep $jobid) &&
        echo $obj | jq -e ".[\"ntasks\"] == 4"
'

test_expect_success 'reload the job-info module' '
        flux exec -r all flux module remove job-info &&
        flux exec -r all flux module load job-info
'

test_expect_success 'verify job names preserved across restart' '
        jobid1=`cat taskcount1.id` &&
        jobid2=`cat taskcount2.id` &&
        obj=$(flux job list -s inactive | grep ${jobid1}) &&
        echo $obj | jq -e ".[\"ntasks\"] == 1" &&
        obj=$(flux job list -s inactive | grep ${jobid2}) &&
        echo $obj | jq -e ".[\"ntasks\"] == 4"
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

test_expect_success HAVE_JQ 'list request with empty attrs works' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, flags:0, attrs:[]}" \
          | $RPC job-info.list > list_empty_attrs.out &&
        test_must_fail grep "userid" list_empty_attrs.out &&
        test_must_fail grep "priority" list_empty_attrs.out &&
        test_must_fail grep "t_submit" list_empty_attrs.out &&
        test_must_fail grep "state" list_empty_attrs.out &&
        test_must_fail grep "name" list_empty_attrs.out &&
        test_must_fail grep "ntasks" list_empty_attrs.out &&
        test_must_fail grep "t_depend" list_empty_attrs.out &&
        test_must_fail grep "t_sched" list_empty_attrs.out &&
        test_must_fail grep "t_run" list_empty_attrs.out &&
        test_must_fail grep "t_cleanup" list_empty_attrs.out &&
        test_must_fail grep "t_inactive" list_empty_attrs.out
'
test_expect_success HAVE_JQ 'list request with excessive max_entries works' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:100000, userid:${id}, flags:0, attrs:[]}" \
          | $RPC job-info.list
'
test_expect_success HAVE_JQ 'list-attrs works' '
        $RPC job-info.list-attrs < /dev/null > list_attrs.out &&
        grep userid list_attrs.out &&
        grep priority list_attrs.out &&
        grep t_submit list_attrs.out &&
        grep state list_attrs.out &&
        grep name list_attrs.out &&
        grep ntasks list_attrs.out &&
        grep t_depend list_attrs.out &&
        grep t_sched list_attrs.out &&
        grep t_run list_attrs.out &&
        grep t_cleanup list_attrs.out &&
        grep t_inactive list_attrs.out
'

#
# job eventlog tests
#

test_expect_success 'flux job eventlog works' '
        jobid=$(submit_job) &&
	flux job eventlog $jobid > eventlog_a.out &&
        grep submit eventlog_a.out
'

test_expect_success 'flux job eventlog works on multiple entries' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foo &&
	flux job eventlog $jobid >eventlog_b.out &&
	grep -q submit eventlog_b.out &&
	grep -q foo eventlog_b.out
'

test_expect_success 'flux job eventlog fails on bad id' '
	! flux job eventlog 12345
'

test_expect_success 'flux job eventlog --format=json works' '
        jobid=$(submit_job) &&
	flux job eventlog --format=json $jobid > eventlog_format1.out &&
        grep -q "\"name\":\"submit\"" eventlog_format1.out &&
        grep -q "\"userid\":$(id -u)" eventlog_format1.out
'

test_expect_success 'flux job eventlog --format=text works' '
        jobid=$(submit_job) &&
	flux job eventlog --format=text $jobid > eventlog_format2.out &&
        grep -q "submit" eventlog_format2.out &&
        grep -q "userid=$(id -u)" eventlog_format2.out
'

test_expect_success 'flux job eventlog --format=invalid fails' '
        jobid=$(submit_job) &&
	! flux job eventlog --format=invalid $jobid
'

test_expect_success 'flux job eventlog --time-format=raw works' '
        jobid=$(submit_job) &&
	flux job eventlog --time-format=raw $jobid > eventlog_time_format1.out &&
        get_timestamp_field submit eventlog_time_format1.out | grep "\."
'

test_expect_success 'flux job eventlog --time-format=iso works' '
        jobid=$(submit_job) &&
	flux job eventlog --time-format=iso $jobid > eventlog_time_format2.out &&
        get_timestamp_field submit eventlog_time_format2.out | grep T | grep Z
'

test_expect_success 'flux job eventlog --time-format=offset works' '
        jobid=$(submit_job) &&
	flux job eventlog --time-format=offset $jobid > eventlog_time_format3.out &&
        get_timestamp_field submit eventlog_time_format3.out | grep "0.000000" &&
        get_timestamp_field exception eventlog_time_format3.out | grep -v "0.000000"
'

test_expect_success 'flux job eventlog --time-format=invalid fails works' '
        jobid=$(submit_job) &&
	! flux job eventlog --time-format=invalid $jobid
'

test_expect_success 'flux job eventlog -p works' '
        jobid=$(submit_job) &&
        flux job eventlog -p "eventlog" $jobid > eventlog_path1.out &&
        grep submit eventlog_path1.out
'

test_expect_success 'flux job eventlog -p works (guest.exec.eventlog)' '
        jobid=$(submit_job) &&
        flux job eventlog -p "guest.exec.eventlog" $jobid > eventlog_path2.out &&
        grep done eventlog_path2.out
'

test_expect_success 'flux job eventlog -p fails on invalid path' '
        jobid=$(submit_job) &&
        ! flux job eventlog -p "foobar" $jobid
'

#
# job wait-event tests
#

test_expect_success 'flux job wait-event works' '
        jobid=$(submit_job) &&
        flux job wait-event $jobid submit > wait_event1.out &&
        grep submit wait_event1.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event errors on non-event' '
        jobid=$(submit_job) &&
        ! flux job wait-event $jobid foobar 2> wait_event2.err &&
        grep "never received" wait_event2.err
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event does not see event after clean' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        ! flux job wait-event -v $jobid foobar 2> wait_event3.err &&
        grep "never received" wait_event3.err
'

test_expect_success 'flux job wait-event fails on bad id' '
	! flux job wait-event 12345 foobar
'

test_expect_success 'flux job wait-event --quiet works' '
        jobid=$(submit_job) &&
        flux job wait-event --quiet $jobid submit > wait_event4.out &&
        ! test -s wait_event4.out
'

test_expect_success 'flux job wait-event --verbose works' '
        jobid=$(submit_job) &&
        flux job wait-event --verbose $jobid clean > wait_event5.out &&
        grep submit wait_event5.out &&
        grep start wait_event5.out &&
        grep clean wait_event5.out
'

test_expect_success 'flux job wait-event --verbose doesnt show events after wait event' '
        jobid=$(submit_job) &&
        flux job wait-event --verbose $jobid submit > wait_event6.out &&
        grep submit wait_event6.out &&
        ! grep start wait_event6.out &&
        ! grep clean wait_event6.out
'

test_expect_success 'flux job wait-event --timeout works' '
        jobid=$(submit_job_live sleeplong.json) &&
        ! flux job wait-event --timeout=0.2 $jobid clean 2> wait_event7.err &&
        flux job cancel $jobid &&
        grep "wait-event timeout" wait_event7.err
'

test_expect_success 'flux job wait-event hangs on no event' '
        jobid=$(submit_job) &&
        ! run_timeout 0.2 flux job wait-event $jobid foobar
'

test_expect_success 'flux job wait-event --format=json works' '
        jobid=$(submit_job) &&
	flux job wait-event --format=json $jobid submit > wait_event_format1.out &&
        grep -q "\"name\":\"submit\"" wait_event_format1.out &&
        grep -q "\"userid\":$(id -u)" wait_event_format1.out
'

test_expect_success 'flux job wait-event --format=text works' '
        jobid=$(submit_job) &&
	flux job wait-event --format=text $jobid submit > wait_event_format2.out &&
        grep -q "submit" wait_event_format2.out &&
        grep -q "userid=$(id -u)" wait_event_format2.out
'

test_expect_success 'flux job wait-event --format=invalid fails' '
        jobid=$(submit_job) &&
	! flux job wait-event --format=invalid $jobid submit
'

test_expect_success 'flux job wait-event --time-format=raw works' '
        jobid=$(submit_job) &&
	flux job wait-event --time-format=raw $jobid submit > wait_event_time_format1.out &&
        get_timestamp_field submit wait_event_time_format1.out | grep "\."
'

test_expect_success 'flux job wait-event --time-format=iso works' '
        jobid=$(submit_job) &&
	flux job wait-event --time-format=iso $jobid submit > wait_event_time_format2.out &&
        get_timestamp_field submit wait_event_time_format2.out | grep T | grep Z
'

test_expect_success 'flux job wait-event --time-format=offset works' '
        jobid=$(submit_job) &&
	flux job wait-event --time-format=offset $jobid submit > wait_event_time_format3A.out &&
        get_timestamp_field submit wait_event_time_format3A.out | grep "0.000000" &&
	flux job wait-event --time-format=offset $jobid exception > wait_event_time_format3B.out &&
        get_timestamp_field exception wait_event_time_format3B.out | grep -v "0.000000"
'

test_expect_success 'flux job wait-event --time-format=invalid fails works' '
        jobid=$(submit_job) &&
	! flux job wait-event --time-format=invalid $jobid submit
'

test_expect_success 'flux job wait-event w/ match-context works (string w/ quotes)' '
        jobid=$(submit_job) &&
	flux job wait-event --match-context="type=\"cancel\"" $jobid exception > wait_event_context1.out &&
        grep -q "exception" wait_event_context1.out &&
        grep -q "type=\"cancel\"" wait_event_context1.out
'

test_expect_success 'flux job wait-event w/ match-context works (string w/o quotes)' '
        jobid=$(submit_job) &&
	flux job wait-event --match-context=type=cancel $jobid exception > wait_event_context2.out &&
        grep -q "exception" wait_event_context2.out &&
        grep -q "type=\"cancel\"" wait_event_context2.out
'

test_expect_success 'flux job wait-event w/ match-context works (int)' '
        jobid=$(submit_job) &&
	flux job wait-event --match-context=flags=0 $jobid submit > wait_event_context3.out &&
        grep -q "submit" wait_event_context3.out &&
        grep -q "flags=0" wait_event_context3.out
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid key)' '
        jobid=$(submit_job) &&
        ! run_timeout 0.2 flux job wait-event --match-context=foo=bar $jobid exception
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid value)' '
        jobid=$(submit_job) &&
        ! run_timeout 0.2 flux job wait-event --match-context=type=foo $jobid exception
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid input)' '
        jobid=$(submit_job) &&
        ! flux job wait-event --match-context=foo $jobid exception
'

test_expect_success 'flux job wait-event -p works (eventlog)' '
        jobid=$(submit_job) &&
        flux job wait-event -p "eventlog" $jobid submit > wait_event_path1.out &&
        grep submit wait_event_path1.out
'

test_expect_success 'flux job wait-event -p works (guest.exec.eventlog)' '
        jobid=$(submit_job) &&
        flux job wait-event -p "guest.exec.eventlog" $jobid done > wait_event_path2.out &&
        grep done wait_event_path2.out
'

test_expect_success 'flux job wait-event -p works (non-guest eventlog)' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.foobar.eventlog foobar &&
        flux job wait-event -p "foobar.eventlog" $jobid foobar > wait_event_path3.out &&
        grep foobar wait_event_path3.out
'

test_expect_success 'flux job wait-event -p fails on invalid path' '
        jobid=$(submit_job) &&
        ! flux job wait-event -p "foobar" $jobid submit
'

test_expect_success 'flux job wait-event -p fails on path "guest."' '
        jobid=$(submit_job) &&
        ! flux job wait-event -p "guest." $jobid submit
'

test_expect_success 'flux job wait-event -p hangs on non-guest eventlog' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.foobar.eventlog foo &&
        ! run_timeout 0.2 flux job wait-event -p "foobar.eventlog" $jobid bar
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.exec.eventlog works (live job)' '
        jobid=$(submit_job_live sleeplong.json)
        flux job wait-event -p "guest.exec.eventlog" $jobid done > wait_event_path4.out &
        waitpid=$! &&
        wait_watchers_nonzero "watchers" &&
        wait_watchers_nonzero "guest_watchers" &&
        guestns=$(flux job namespace $jobid) &&
        wait_watcherscount_nonzero $guestns &&
        flux job cancel $jobid &&
        wait $waitpid &&
        grep done wait_event_path4.out
'

test_expect_success 'flux job wait-event -p times out on no event (live job)' '
        jobid=$(submit_job_live sleeplong.json) &&
        ! run_timeout 0.2 flux job wait-event -p "guest.exec.eventlog" $jobid foobar &&
        flux job cancel $jobid
'

# In order to test watching a guest event log that does not yet exist,
# we will start a job that will take up all resources.  Then start
# another job, which we will watch and know it hasn't started running
# yet. Then we cancel the initial job to get the new one running.

test_expect_success 'job-info: generate jobspec to consume all resources' '
        flux jobspec --format json srun -n4 -c2 sleep 300 > sleeplong-all-rsrc.json
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.exec.eventlog works (wait job)' '
        jobidall=$(submit_job_live sleeplong-all-rsrc.json)
        jobid=$(submit_job_wait)
        flux job wait-event -v -p "guest.exec.eventlog" ${jobid} done > wait_event_path5.out &
        waitpid=$! &&
        wait_watchers_nonzero "watchers" &&
        wait_watchers_nonzero "guest_watchers" &&
        flux job cancel ${jobidall} &&
        flux job wait-event ${jobid} start &&
        guestns=$(flux job namespace ${jobid}) &&
        wait_watcherscount_nonzero $guestns &&
        flux job cancel ${jobid} &&
        wait $waitpid &&
        grep done wait_event_path5.out
'

test_expect_success 'flux job wait-event -p times out on no event (wait job)' '
        jobidall=$(submit_job_live sleeplong-all-rsrc.json) &&
        jobid=$(submit_job_wait) &&
        ! run_timeout 0.2 flux job wait-event -p "guest.exec.eventlog" $jobid foobar &&
        flux job cancel $jobidall &&
        flux job cancel $jobid
'

# In order to test watching a guest event log that will never exist,
# we will start a job that will take up all resources.  Then start
# another job, which we will watch and know it hasn't started running
# yet. Then we cancel the second job before we know it has started.

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.exec.eventlog works (never start job)' '
        jobidall=$(submit_job_live sleeplong-all-rsrc.json)
        jobid=$(submit_job_wait)
        flux job wait-event -v -p "guest.exec.eventlog" ${jobid} done > wait_event_path6.out &
        waitpid=$! &&
        wait_watchers_nonzero "watchers" &&
        wait_watchers_nonzero "guest_watchers" &&
        flux job cancel ${jobid} &&
        ! wait $waitpid &&
        flux job cancel ${jobidall}
'

#
# job info tests
#

test_expect_success 'flux job info eventlog works' '
        jobid=$(submit_job) &&
	flux job info $jobid eventlog > eventlog_info_a.out &&
        grep submit eventlog_info_a.out
'

test_expect_success 'flux job info eventlog fails on bad id' '
	! flux job info 12345 eventlog
'

test_expect_success 'flux job info jobspec works' '
        jobid=$(submit_job) &&
	flux job info $jobid jobspec > jobspec_a.out &&
        grep sleep jobspec_a.out
'

test_expect_success 'flux job info jobspec fails on bad id' '
	! flux job info 12345 jobspec
'

#
# job info tests (multiple info requests)
#

test_expect_success 'flux job info multiple keys works' '
        jobid=$(submit_job) &&
	flux job info $jobid eventlog jobspec J > all_info_a.out &&
        grep submit all_info_a.out &&
        grep sleep all_info_a.out
'

test_expect_success 'flux job info multiple keys fails on bad id' '
	! flux job info 12345 eventlog jobspec J
'

test_expect_success 'flux job info multiple keys fails on 1 bad entry (include eventlog)' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
        flux kvs unlink ${kvsdir}.jobspec &&
	! flux job info $jobid eventlog jobspec J > all_info_b.out
'

test_expect_success 'flux job info multiple keys fails on 1 bad entry (no eventlog)' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
        flux kvs unlink ${kvsdir}.jobspec &&
	! flux job info $jobid jobspec J > all_info_b.out
'

#
# stats
#

test_expect_success 'job-info stats works' '
        flux module stats --parse lookups job-info &&
        flux module stats --parse watchers job-info &&
        flux module stats --parse guest_watchers job-info &&
        flux module stats --parse jobs.pending job-info &&
        flux module stats --parse jobs.running job-info &&
        flux module stats --parse jobs.inactive job-info
'

test_expect_success 'lookup request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.lookup 71 </dev/null
'
test_expect_success 'eventlog-watch request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.eventlog-watch 71 </dev/null
'
test_expect_success 'guest-eventlog-watch request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.guest-eventlog-watch 71 </dev/null
'
test_expect_success 'list request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.list 71 </dev/null
'
test_expect_success HAVE_JQ 'list request with invalid input fails with EPROTO(71) (attrs not an array)' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, flags:0, attrs:5}" \
          | $RPC job-info.list 71
'
test_expect_success HAVE_JQ 'list request with invalid input fails with EINVAL(22) (attrs non-string)' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, flags:0, attrs:[5]}" \
          | $RPC job-info.list 22
'
test_expect_success HAVE_JQ 'list request with invalid input fails with EINVAL(22) (attrs illegal field)' '
        id=$(id -u) &&
        $jq -j -c -n  "{max_entries:5, userid:${id}, flags:0, attrs:[\"foo\"]}" \
          | $RPC job-info.list 22
'

#
# cleanup
#
test_expect_success 'remove sched-simple,job-exec modules' '
        flux exec -r all flux module remove barrier &&
        flux module remove sched-simple &&
        flux module remove job-exec
'

test_done
