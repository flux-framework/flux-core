#!/bin/sh

test_description='Test flux job info service'

. `dirname $0`/kvs/kvs-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc

# Usage: submit_job
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.
submit_job() {
        jobid=$(flux job submit test.json)
        flux job cancel $jobid
        flux job wait-event $jobid clean >/dev/null
        echo $jobid
}

wait_watchers_nonzero() {
        i=0
        while (! flux module stats --parse watchers job-info > /dev/null 2>&1 \
               || [ "$(flux module stats --parse watchers job-info 2> /dev/null)" = "0" ]) \
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
        field=$1
        file=$2
        grep $field $file | awk '{print $1}'
}

test_expect_success 'job-info: generate jobspec for simple test job' '
        flux jobspec --format json srun -N1 sleep inf > test.json
'

hwloc_fake_config='{"0-1":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'load job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load -r 0 sched-simple &&
        flux module load -r 0 job-exec
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

test_expect_success NO_CHAIN_LINT 'flux job wait-event works, event is later' '
        jobid=$(submit_job)
        flux job wait-event $jobid foobar > wait_event3.out &
        waitpid=$! &&
        wait_watchers_nonzero &&
        wait_watcherscount_nonzero primary &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        wait $waitpid &&
        grep foobar wait_event3.out
'

test_expect_success 'flux job wait-event fails on bad id' '
	! flux job wait-event 12345 foobar
'

test_expect_success 'flux job wait-event --quiet works' '
        jobid=$(submit_job) &&
        flux job wait-event --quiet $jobid submit > wait_event7.out &&
        ! test -s wait_event7.out
'

test_expect_success 'flux job wait-event --verbose works' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobaz &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        flux job wait-event --verbose $jobid foobar > wait_event8.out &&
        grep submit wait_event8.out &&
        grep foobaz wait_event8.out &&
        grep foobar wait_event8.out
'

test_expect_success 'flux job wait-event --verbose doesnt show events after wait event' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        flux job wait-event --verbose $jobid submit > wait_event9.out &&
        grep submit wait_event9.out &&
        ! grep foobar wait_event9.out
'

test_expect_success 'flux job wait-event --timeout works' '
        jobid=$(submit_job) &&
        ! flux job wait-event --timeout=0.2 $jobid foobar 2> wait_event8.err &&
        grep "wait-event timeout" wait_event8.err
'

test_expect_success 'flux job wait-event hangs on no event' '
        jobid=$(submit_job) &&
        ! run_timeout 0.2 flux job wait-event $jobid foobar
'

test_expect_success 'flux job wait-event --format=json works' '
        jobid=$(submit_job) &&
	flux job wait-event --format=json $jobid submit > wait_event_format1.out &&
        grep -q "\"name\":\"submit\"" eventlog_format1.out &&
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
        flux module stats job-info | grep "lookups" &&
        flux module stats job-info | grep "watchers"
'

test_expect_success 'lookup request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.lookup 71 </dev/null
'
test_expect_success 'eventlog-watch request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.eventlog-watch 71 </dev/null
'

#
# cleanup
#
test_expect_success 'remove sched-simple,job-exec modules' '
        flux module remove -r 0 sched-simple &&
        flux module remove -r 0 job-exec
'

test_done
