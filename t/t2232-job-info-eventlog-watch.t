#!/bin/sh

test_description='Test flux job info service eventlog watch'

. `dirname $0`/kvs/kvs-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

# Usage: submit_job
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.
submit_job() {
        local jobid=$(flux job submit sleeplong.json) &&
        fj_wait_event $jobid start >/dev/null &&
        flux job cancel $jobid &&
        fj_wait_event $jobid clean >/dev/null &&
        echo $jobid
}

# Unlike above, do not cancel the job, the test will cancel the job
submit_job_live() {
        local jobspec=$1
        local jobid=$(flux job submit $jobspec) &&
        fj_wait_event $jobid start >/dev/null &&
        echo $jobid
}

# Test will cancel the job, is assumed won't run immediately
submit_job_wait() {
        local jobid=$(flux job submit sleeplong.json) &&
        fj_wait_event $jobid depend >/dev/null &&
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
        flux jobspec --format json srun -N1 sleep 300 > sleeplong.json
'

test_expect_success 'flux job wait-event works' '
        jobid=$(submit_job) &&
        fj_wait_event $jobid submit > wait_event1.out &&
        grep submit wait_event1.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event errors on non-event' '
        jobid=$(submit_job) &&
        test_must_fail fj_wait_event $jobid foobar 2> wait_event2.err &&
        grep "never received" wait_event2.err
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event does not see event after clean' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        test_must_fail fj_wait_event -v $jobid foobar 2> wait_event3.err &&
        grep "never received" wait_event3.err
'

test_expect_success 'flux job wait-event fails on bad id' '
	test_must_fail fj_wait_event 12345 foobar
'

test_expect_success 'flux job wait-event --quiet works' '
        jobid=$(submit_job) &&
        fj_wait_event --quiet $jobid submit > wait_event4.out &&
        ! test -s wait_event4.out
'

test_expect_success 'flux job wait-event --verbose works' '
        jobid=$(submit_job) &&
        fj_wait_event --verbose $jobid clean > wait_event5.out &&
        grep submit wait_event5.out &&
        grep start wait_event5.out &&
        grep clean wait_event5.out
'

test_expect_success 'flux job wait-event --verbose doesnt show events after wait event' '
        jobid=$(submit_job) &&
        fj_wait_event --verbose $jobid submit > wait_event6.out &&
        grep submit wait_event6.out &&
        ! grep start wait_event6.out &&
        ! grep clean wait_event6.out
'

test_expect_success 'flux job wait-event --timeout works' '
        jobid=$(submit_job_live sleeplong.json) &&
        test_must_fail flux job wait-event --timeout=0.2 $jobid clean 2> wait_event7.err &&
        flux job cancel $jobid &&
        grep "wait-event timeout" wait_event7.err
'

test_expect_success 'flux job wait-event --format=json works' '
        jobid=$(submit_job) &&
	fj_wait_event --format=json $jobid submit > wait_event_format1.out &&
        grep -q "\"name\":\"submit\"" wait_event_format1.out &&
        grep -q "\"userid\":$(id -u)" wait_event_format1.out
'

test_expect_success 'flux job wait-event --format=text works' '
        jobid=$(submit_job) &&
	fj_wait_event --format=text $jobid submit > wait_event_format2.out &&
        grep -q "submit" wait_event_format2.out &&
        grep -q "userid=$(id -u)" wait_event_format2.out
'

test_expect_success 'flux job wait-event --format=invalid fails' '
        jobid=$(submit_job) &&
	test_must_fail fj_wait_event --format=invalid $jobid submit
'

test_expect_success 'flux job wait-event --time-format=raw works' '
        jobid=$(submit_job) &&
	fj_wait_event --time-format=raw $jobid submit > wait_event_time_format1.out &&
        get_timestamp_field submit wait_event_time_format1.out | grep "\."
'

test_expect_success 'flux job wait-event --time-format=iso works' '
        jobid=$(submit_job) &&
	fj_wait_event --time-format=iso $jobid submit > wait_event_time_format2.out &&
        get_timestamp_field submit wait_event_time_format2.out | grep T | grep Z
'

test_expect_success 'flux job wait-event --time-format=offset works' '
        jobid=$(submit_job) &&
	fj_wait_event --time-format=offset $jobid submit > wait_event_time_format3A.out &&
        get_timestamp_field submit wait_event_time_format3A.out | grep "0.000000" &&
	fj_wait_event --time-format=offset $jobid exception > wait_event_time_format3B.out &&
        get_timestamp_field exception wait_event_time_format3B.out | grep -v "0.000000"
'

test_expect_success 'flux job wait-event --time-format=invalid fails works' '
        jobid=$(submit_job) &&
	test_must_fail fj_wait_event --time-format=invalid $jobid submit
'

test_expect_success 'flux job wait-event w/ match-context works (string w/ quotes)' '
        jobid=$(submit_job) &&
	fj_wait_event --match-context="type=\"cancel\"" $jobid exception > wait_event_context1.out &&
        grep -q "exception" wait_event_context1.out &&
        grep -q "type=\"cancel\"" wait_event_context1.out
'

test_expect_success 'flux job wait-event w/ match-context works (string w/o quotes)' '
        jobid=$(submit_job) &&
	fj_wait_event --match-context=type=cancel $jobid exception > wait_event_context2.out &&
        grep -q "exception" wait_event_context2.out &&
        grep -q "type=\"cancel\"" wait_event_context2.out
'

test_expect_success 'flux job wait-event w/ match-context works (int)' '
        jobid=$(submit_job) &&
	fj_wait_event --match-context=flags=0 $jobid submit > wait_event_context3.out &&
        grep -q "submit" wait_event_context3.out &&
        grep -q "flags=0" wait_event_context3.out
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid key)' '
        jobid=$(submit_job) &&
        test_must_fail fj_wait_event --match-context=foo=bar $jobid exception
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid value)' '
        jobid=$(submit_job) &&
        test_must_fail fj_wait_event --match-context=type=foo $jobid exception
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid input)' '
        jobid=$(submit_job) &&
        test_must_fail fj_wait_event --match-context=foo $jobid exception
'

test_expect_success 'flux job wait-event -p works (eventlog)' '
        jobid=$(submit_job) &&
        fj_wait_event -p "eventlog" $jobid submit > wait_event_path1.out &&
        grep submit wait_event_path1.out
'

test_expect_success 'flux job wait-event -p works (guest.exec.eventlog)' '
        jobid=$(submit_job) &&
        fj_wait_event -p "guest.exec.eventlog" $jobid done > wait_event_path2.out &&
        grep done wait_event_path2.out
'

test_expect_success 'flux job wait-event -p works (non-guest eventlog)' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.foobar.eventlog foobar &&
        fj_wait_event -p "foobar.eventlog" $jobid foobar > wait_event_path3.out &&
        grep foobar wait_event_path3.out
'

test_expect_success 'flux job wait-event -p fails on invalid path' '
        jobid=$(submit_job) &&
        test_must_fail fj_wait_event -p "foobar" $jobid submit
'

test_expect_success 'flux job wait-event -p fails on path "guest."' '
        jobid=$(submit_job) &&
        test_must_fail fj_wait_event -p "guest." $jobid submit
'

test_expect_success 'flux job wait-event -p hangs on non-guest eventlog' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.foobar.eventlog foo &&
        test_expect_code 142 run_timeout -s ALRM 0.2 flux job wait-event -p "foobar.eventlog" $jobid bar
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.exec.eventlog works (live job)' '
        jobid=$(submit_job_live sleeplong.json)
        fj_wait_event -p "guest.exec.eventlog" $jobid done > wait_event_path4.out &
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
        test_expect_code 142 run_timeout -s ALRM 0.2 flux job wait-event -p "guest.exec.eventlog" $jobid foobar &&
        flux job cancel $jobid
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event --count=0 errors' '
        test_must_fail fj_wait_event --count=0 1234 foobar 2> count.err &&
        grep "count must be" count.err
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event --count=1 works' '
        jobid=$(submit_job) &&
        fj_wait_event --count=1 ${jobid} clean
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event --count=2 works' '
        jobid=$(submit_job_wait) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        test_must_fail flux job wait-event --timeout=0.2 --count=2 ${jobid} foobar &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        fj_wait_event --count=2 ${jobid} foobar &&
        flux job cancel $jobid
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event --count=2 and context match works' '
        jobid=$(submit_job_wait) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
        test_must_fail flux job wait-event --timeout=0.2 --count=2 --match-context="foo=\"bar\"" ${jobid} foobar &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
        fj_wait_event --count=2 --match-context="foo=\"bar\"" ${jobid} foobar &&
        flux job cancel $jobid
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event --count=2 and invalid context match fails' '
        jobid=$(submit_job_wait) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
        test_must_fail flux job wait-event --timeout=0.2 --count=2 --match-context="foo=\"dar\"" ${jobid} foobar &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
        test_must_fail flux job wait-event --timeout=0.2 --count=2 --match-context="foo=\"dar\"" ${jobid} foobar &&
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
        fj_wait_event -v -p "guest.exec.eventlog" ${jobid} done > wait_event_path5.out &
        waitpid=$! &&
        wait_watchers_nonzero "watchers" &&
        wait_watchers_nonzero "guest_watchers" &&
        flux job cancel ${jobidall} &&
        fj_wait_event ${jobid} start &&
        guestns=$(flux job namespace ${jobid}) &&
        wait_watcherscount_nonzero $guestns &&
        flux job cancel ${jobid} &&
        wait $waitpid &&
        grep done wait_event_path5.out
'

test_expect_success 'flux job wait-event -p times out on no event (wait job)' '
        jobidall=$(submit_job_live sleeplong-all-rsrc.json) &&
        jobid=$(submit_job_wait) &&
        test_expect_code 142 run_timeout -s ALRM 0.2 flux job wait-event -p "guest.exec.eventlog" $jobid foobar &&
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
        fj_wait_event -v -p "guest.exec.eventlog" ${jobid} done > wait_event_path6.out &
        waitpid=$! &&
        wait_watchers_nonzero "watchers" &&
        wait_watchers_nonzero "guest_watchers" &&
        flux job cancel ${jobid} &&
        ! wait $waitpid &&
        flux job cancel ${jobidall}
'

#
# stats & corner cases
#

test_expect_success 'job-info stats works' '
        flux module stats --parse watchers job-info &&
        flux module stats --parse guest_watchers job-info
'

test_expect_success 'eventlog-watch request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.eventlog-watch 71 </dev/null
'
test_expect_success 'guest-eventlog-watch request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.guest-eventlog-watch 71 </dev/null
'

test_done
