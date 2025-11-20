#!/bin/sh

test_description='Test flux job info service eventlog watch'

. `dirname $0`/kvs/kvs-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc
RPC_STREAM=${FLUX_BUILD_DIR}/t/request/rpc_stream
WATCHSENTINEL=${FLUX_BUILD_DIR}/t/job-info/eventlog_watch_initial_sentinel
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

fj_wait_event() {
	flux job wait-event --timeout=20 "$@"
}

# Usage: submit_job
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.
submit_job() {
	local jobid=$(flux job submit sleeplong.json) &&
	fj_wait_event $jobid start >/dev/null &&
	flux cancel $jobid &&
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
	flux run --dry-run -n1 -N1 sleep 300 > sleeplong.json
'

test_expect_success 'submit and cancel job for test use' '
	JOBID=$(submit_job)
'

test_expect_success 'flux job wait-event works' '
	fj_wait_event $JOBID submit > wait_event1.out &&
	grep submit wait_event1.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event errors on non-event' '
	test_must_fail fj_wait_event $JOBID foobar 2> wait_event2.err &&
	grep "never received" wait_event2.err
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event does not see event after clean' '
	kvsdir=$(flux job id --to=kvs $JOBID) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
	test_must_fail fj_wait_event -v $JOBID foobar 2> wait_event3.err &&
	grep "never received" wait_event3.err
'

test_expect_success 'flux job wait-event fails on bad id' '
	test_must_fail fj_wait_event 12345 foobar
'

test_expect_success 'flux job wait-event --quiet works' '
	fj_wait_event --quiet $JOBID submit > wait_event4.out &&
	! test -s wait_event4.out
'

test_expect_success 'flux job wait-event --verbose works' '
	fj_wait_event --verbose $JOBID clean > wait_event5.out &&
	grep submit wait_event5.out &&
	grep start wait_event5.out &&
	grep clean wait_event5.out
'

test_expect_success 'flux job wait-event --verbose doesnt show events after wait event' '
	fj_wait_event --verbose $JOBID submit > wait_event6.out &&
	grep submit wait_event6.out &&
	! grep start wait_event6.out &&
	! grep clean wait_event6.out
'

test_expect_success 'flux job wait-event --timeout works' '
	jobid=$(submit_job_live sleeplong.json) &&
	test_must_fail flux job wait-event --timeout=0.2 $jobid clean 2> wait_event7.err &&
	flux cancel $jobid &&
	grep "wait-event timeout" wait_event7.err
'

test_expect_success 'flux job wait-event --format=json works' '
	fj_wait_event --format=json $JOBID submit > wait_event_format1.out &&
	grep -q "\"name\":\"submit\"" wait_event_format1.out &&
	grep -q "\"userid\":$(id -u)" wait_event_format1.out
'

test_expect_success 'flux job wait-event --format=text works' '
	fj_wait_event --format=text $JOBID submit > wait_event_format2.out &&
	grep -q "submit" wait_event_format2.out &&
	grep -q "userid=$(id -u)" wait_event_format2.out
'

test_expect_success 'flux job wait-event --format=invalid fails' '
	test_must_fail fj_wait_event --format=invalid $JOBID submit
'

test_expect_success 'flux job wait-event --time-format=raw works' '
	fj_wait_event --time-format=raw $JOBID submit > wait_event_time_format1.out &&
	get_timestamp_field submit wait_event_time_format1.out | grep "\."
'

test_expect_success 'flux job wait-event --time-format=iso works' '
	fj_wait_event --time-format=iso $JOBID submit > wait_event_time_format2.out &&
	get_timestamp_field submit wait_event_time_format2.out | grep T | grep Z
'

test_expect_success 'flux job wait-event --time-format=offset works' '
	fj_wait_event --time-format=offset $JOBID submit > wait_event_time_format3A.out &&
	get_timestamp_field submit wait_event_time_format3A.out | grep "0.000000" &&
	fj_wait_event --time-format=offset $JOBID exception > wait_event_time_format3B.out &&
	get_timestamp_field exception wait_event_time_format3B.out | grep -v "0.000000"
'

test_expect_success 'flux job wait-event --time-format=invalid fails works' '
	test_must_fail fj_wait_event --time-format=invalid $JOBID submit
'

test_expect_success 'flux job wait-event w/ match-context works (string w/ quotes)' '
	fj_wait_event --match-context="type=\"cancel\"" $JOBID exception > wait_event_context1.out &&
	grep -q "exception" wait_event_context1.out &&
	grep -q "type=\"cancel\"" wait_event_context1.out
'

test_expect_success 'flux job wait-event w/ match-context works (string w/o quotes)' '
	fj_wait_event --match-context=type=cancel $JOBID exception > wait_event_context2.out &&
	grep -q "exception" wait_event_context2.out &&
	grep -q "type=\"cancel\"" wait_event_context2.out
'

test_expect_success 'flux job wait-event w/ match-context works (int)' '
	fj_wait_event --match-context=flags=0 $JOBID submit > wait_event_context3.out &&
	grep -q "submit" wait_event_context3.out &&
	grep -q "flags=0" wait_event_context3.out
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid key)' '
	test_must_fail fj_wait_event --match-context=foo=bar $JOBID exception
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid value)' '
	test_must_fail fj_wait_event --match-context=type=foo $JOBID exception
'

# Note: in test below, foo=0 would match severity=0 in buggy version
test_expect_success 'flux job wait-event w/ bad match-context fails (issue #5845)' '
	test_must_fail fj_wait_event --match-context=foo=0 $JOBID exception
'

test_expect_success 'flux job wait-event w/ bad match-context fails (invalid input)' '
	test_must_fail fj_wait_event --match-context=foo $JOBID exception
'

test_expect_success 'flux job wait-event -p works (eventlog)' '
	fj_wait_event -p "eventlog" $JOBID submit > wait_event_path1.out &&
	grep submit wait_event_path1.out
'

test_expect_success 'flux job wait-event -p works (guest.exec.eventlog)' '
	fj_wait_event -p "guest.exec.eventlog" $JOBID done > wait_event_path2.out &&
	grep done wait_event_path2.out
'
test_expect_success 'flux job wait-event -p works (exec)' '
	fj_wait_event -p exec $JOBID done > wait_event_path2.1.out &&
	grep done wait_event_path2.1.out
'
test_expect_success 'flux job wait-event -p works (non-guest eventlog)' '
	kvsdir=$(flux job id --to=kvs $JOBID) &&
	flux kvs eventlog append ${kvsdir}.foobar.eventlog foobar &&
	fj_wait_event -p "foobar.eventlog" $JOBID foobar > wait_event_path3.out &&
	grep foobar wait_event_path3.out
'

test_expect_success 'flux job wait-event -p fails on invalid path' '
	test_must_fail fj_wait_event -p "foobar" $JOBID submit
'

test_expect_success 'flux job wait-event -p fails on invalid guest path' '
	test_must_fail fj_wait_event -p "guest.foobar" $JOBID submit
'

test_expect_success 'flux job wait-event -p fails on path "guest."' '
	test_must_fail fj_wait_event -p "guest." $JOBID submit
'

test_expect_success 'flux job wait-event -p and --waitcreate works (exec)' '
	fj_wait_event --waitcreate -p exec $JOBID done > wait_event_path4.out &&
	grep done wait_event_path4.out
'

test_expect_success 'flux job wait-event -p invalid and --waitcreate fails' '
	test_must_fail fj_wait_event --waitcreate -p "guest.invalid" $JOBID foobar 2> waitcreate1.err &&
	grep "not found" waitcreate1.err
'

test_expect_success 'flux job wait-event -p hangs on non-guest eventlog' '
	kvsdir=$(flux job id --to=kvs $JOBID) &&
	flux kvs eventlog append ${kvsdir}.foobar.eventlog foo &&
	test_expect_code 142 run_timeout -s ALRM 0.2 flux job wait-event -p "foobar.eventlog" $JOBID bar
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.exec.eventlog works (live job)' '
	jobid=$(submit_job_live sleeplong.json)
	fj_wait_event -p "guest.exec.eventlog" $jobid done > wait_event_path5.out &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	guestns=$(flux job namespace $jobid) &&
	wait_watcherscount_nonzero $guestns &&
	flux cancel $jobid &&
	wait $waitpid &&
	grep done wait_event_path5.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.foobar and --waitcreate works (live job)' '
	jobid=$(submit_job_live sleeplong.json)
	fj_wait_event --waitcreate -p "guest.foobar" $jobid foobar > wait_event_path6.out &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	guestns=$(flux job namespace $jobid) &&
	wait_watcherscount_nonzero $guestns &&
	flux kvs eventlog append --namespace=${guestns} foobar foobar &&
	flux cancel $jobid &&
	wait $waitpid &&
	grep foobar wait_event_path6.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p invalid and --waitcreate fails (live job)' '
	jobid=$(submit_job_live sleeplong.json)
	fj_wait_event --waitcreate -p "guest.invalid" $jobid foobar \
		> wait_event_path6.out 2> wait_event_path6.err &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	guestns=$(flux job namespace $jobid) &&
	wait_watcherscount_nonzero $guestns &&
	flux cancel $jobid &&
	! wait $waitpid &&
	grep "not found" wait_event_path6.err
'

test_expect_success 'flux job wait-event -p times out on no event (live job)' '
	jobid=$(submit_job_live sleeplong.json) &&
	test_expect_code 142 run_timeout -s ALRM 0.2 \
	    flux job wait-event -p exec $jobid foobar &&
	flux cancel $jobid
'

test_expect_success 'flux job wait-event --count=0 errors' '
	test_must_fail fj_wait_event --count=0 1234 foobar 2> count.err &&
	grep "count must be" count.err
'

test_expect_success 'flux job wait-event --count=1 works' '
	fj_wait_event --count=1 ${JOBID} clean
'

test_expect_success 'flux job wait-event --count=2 works' '
	jobid=$(submit_job_wait) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
	test_must_fail flux job wait-event --timeout=0.2 --count=2 ${jobid} foobar &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
	fj_wait_event --count=2 ${jobid} foobar &&
	flux cancel $jobid
'

test_expect_success 'flux job wait-event --count=2 and context match works' '
	jobid=$(submit_job_wait) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
	test_must_fail flux job wait-event --timeout=0.2 --count=2 --match-context="foo=\"bar\"" ${jobid} foobar &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
	fj_wait_event --count=2 --match-context="foo=\"bar\"" ${jobid} foobar &&
	flux cancel $jobid
'

test_expect_success 'flux job wait-event --count=2 and invalid context match fails' '
	jobid=$(submit_job_wait) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
	test_must_fail flux job wait-event --timeout=0.2 --count=2 --match-context="foo=\"dar\"" ${jobid} foobar &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar "{\"foo\":\"bar\"}" &&
	test_must_fail flux job wait-event --timeout=0.2 --count=2 --match-context="foo=\"dar\"" ${jobid} foobar &&
	flux cancel $jobid
'

# In order to test watching a guest event log that does not yet exist,
# we will start a job that will take up all resources.  Then start
# another job, which we will watch and know it hasn't started running
# yet. Then we cancel the initial job to get the new one running.

test_expect_success 'job-info: generate jobspec to consume all resources' '
	flux run --dry-run -n4 -c2 sleep 300 > sleeplong-all-rsrc.json
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p exec works (wait job)' '
	jobidall=$(submit_job_live sleeplong-all-rsrc.json)
	jobid=$(submit_job_wait)
	fj_wait_event -v -p exec ${jobid} done > wait_event_path7.out &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	flux cancel ${jobidall} &&
	fj_wait_event ${jobid} start &&
	guestns=$(flux job namespace ${jobid}) &&
	wait_watcherscount_nonzero $guestns &&
	flux cancel ${jobid} &&
	wait $waitpid &&
	grep done wait_event_path7.out
'

test_expect_success 'flux job wait-event -p times out on no event (wait job)' '
	jobidall=$(submit_job_live sleeplong-all-rsrc.json) &&
	jobid=$(submit_job_wait) &&
	test_expect_code 142 run_timeout -s ALRM 0.2 \
	    flux job wait-event -p exec $jobid foobar &&
	flux cancel $jobidall &&
	flux cancel $jobid
'

# In order to test watching a guest event log that will never exist,
# we will start a job that will take up all resources.  Then start
# another job, which we will watch and know it hasn't started running
# yet. Then we cancel the second job before we know it has started.

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p guest.exec.eventlog works (never start job)' '
	jobidall=$(submit_job_live sleeplong-all-rsrc.json)
	jobid=$(submit_job_wait)
	fj_wait_event -v -p exec ${jobid} done > wait_event_path8.out &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	flux cancel ${jobid} &&
	! wait $waitpid &&
	flux cancel ${jobidall}
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p and --waitcreate works (wait job)' '
	jobidall=$(submit_job_live sleeplong-all-rsrc.json)
	jobid=$(submit_job_wait)
	fj_wait_event -v --waitcreate -p "guest.foobar" ${jobid} foobar > wait_event_path8.out &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	flux cancel ${jobidall} &&
	fj_wait_event ${jobid} start &&
	guestns=$(flux job namespace ${jobid}) &&
	wait_watcherscount_nonzero $guestns &&
	flux kvs eventlog append --namespace=${guestns} foobar foobar &&
	flux cancel ${jobid} &&
	wait $waitpid &&
	grep foobar wait_event_path8.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p invalid and --waitcreate fails (wait job)' '
	jobidall=$(submit_job_live sleeplong-all-rsrc.json)
	jobid=$(submit_job_wait)
	fj_wait_event -v --waitcreate -p "guest.invalid" ${jobid} invalid \
		> wait_event_path9.out 2> wait_event_path9.err &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	flux cancel ${jobidall} &&
	fj_wait_event ${jobid} start &&
	guestns=$(flux job namespace ${jobid}) &&
	wait_watcherscount_nonzero $guestns &&
	flux cancel ${jobid} &&
	! wait $waitpid &&
	grep "not found" wait_event_path9.err
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event -p invalid and --waitcreate works (never start job)' '
	jobidall=$(submit_job_live sleeplong-all-rsrc.json)
	jobid=$(submit_job_wait)
	fj_wait_event -v --waitcreate -p "guest.invalid" ${jobid} invalid \
		> wait_event_path10.out 2> wait_event_path10.err &
	waitpid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	flux cancel ${jobid} &&
	! wait $waitpid &&
	grep "never received" wait_event_path10.err &&
	flux cancel ${jobidall}
'

#
# initial sentinel flag
#

# N.B. eventlog-watch-initial-sentinel outputs "sentinel" when the initial one
# is received
test_expect_success NO_CHAIN_LINT 'eventlog-watch-initial-sentinel works (main)' '
	jobid=$(submit_job_live sleeplong.json) &&
	flux job wait-event --timeout=20 $jobid start
	${WATCHSENTINEL} ${jobid} eventlog > sentinel1.out &
	pid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watcherscount_nonzero primary &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="start" sentinel1.out >/dev/null &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="sentinel" sentinel1.out >/dev/null &&
	test_debug "cat sentinel1.out" &&
	tail -n 2 sentinel1.out | head -n 1 | grep start &&
	tail -n 1 sentinel1.out | grep sentinel &&
	flux cancel ${jobid} &&
	wait ${pid} &&
	tail -n 1 sentinel1.out | grep clean
'

test_expect_success NO_CHAIN_LINT 'eventlog-watch-initial-sentinel works (guestns)' '
	jobid=$(submit_job_live sleeplong.json) &&
	flux job wait-event --timeout=20 -p exec $jobid "shell.start"
	${WATCHSENTINEL} ${jobid} guest.exec.eventlog > sentinel2.out &
	pid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	guestns=$(flux job namespace $jobid) &&
	wait_watcherscount_nonzero $guestns &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="shell.start" sentinel2.out >/dev/null &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="sentinel" sentinel2.out >/dev/null &&
	test_debug "cat sentinel2.out" &&
	tail -n 2 sentinel2.out | head -n 1 | grep "shell.start" &&
	tail -n 1 sentinel2.out | grep sentinel &&
	flux cancel ${jobid} &&
	wait ${pid} &&
	tail -n 1 sentinel2.out | grep done
'

test_expect_success 'eventlog-watch-initial-sentinel works (completed main)' '
	jobid=$(submit_job sleeplong.json) &&
	${WATCHSENTINEL} ${jobid} eventlog > sentinel3.out &&
	test_debug "cat sentinel3.out" &&
	tail -n 2 sentinel3.out | head -n 1 | grep clean &&
	tail -n 1 sentinel3.out | grep sentinel
'

test_expect_success NO_CHAIN_LINT 'eventlog-watch-initial-sentinel works (completed guestns)' '
	jobid=$(submit_job sleeplong.json)
	${WATCHSENTINEL} ${jobid} guest.exec.eventlog > sentinel4.out
	test_debug "cat sentinel4.out" &&
	tail -n 2 sentinel4.out | head -n 1 | grep "done" &&
	tail -n 1 sentinel4.out | grep sentinel
'

test_expect_success NO_CHAIN_LINT 'eventlog-watch-initial-sentinel fails on bad job id' '
	test_must_fail ${WATCHSENTINEL} 123456789 eventlog
'

test_expect_success NO_CHAIN_LINT 'eventlog-watch-initial-sentinel works w/ WAITCREATE' '
	jobid=$(submit_job_live sleeplong.json)
	${WATCHSENTINEL} --waitcreate ${jobid} guest.foobar > sentinel5.out &
	pid=$! &&
	wait_watchers_nonzero "watchers" &&
	wait_watchers_nonzero "guest_watchers" &&
	guestns=$(flux job namespace $jobid) &&
	wait_watcherscount_nonzero $guestns &&
        flux kvs eventlog append --namespace=${guestns} foobar hello &&
        flux kvs eventlog append --namespace=${guestns} foobar goodbye &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="goodbye" sentinel5.out >/dev/null &&
	test_debug "cat sentinel5.out" &&
	head -n 1 sentinel5.out | grep sentinel &&
	tail -n 2 sentinel5.out | head -n 1 | grep hello &&
	tail -n 1 sentinel5.out | grep goodbye &&
	flux cancel ${jobid} &&
	wait ${pid}
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

test_expect_success 'eventlog-watch request with invalid flags fails with EPROTO(71)' '
	echo "{\"id\":42, \"path\":\"foobar\", \"flags\":499}" \
		| ${RPC_STREAM} job-info.eventlog-watch 71 "invalid flag"
'
test_expect_success 'eventlog-watch request non-streaming fails with EPROTO(71)' '
	echo "{\"id\":42, \"path\":\"foobar\", \"flags\":0}" \
		| ${RPC} job-info.eventlog-watch 71
'

test_done
