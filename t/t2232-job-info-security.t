#!/bin/sh

test_description='Test flux job info service security'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

# We have to fake a job submission by a guest into the KVS.
# This method of editing the eventlog preserves newline separators.

update_job_userid() {
	local userid=$1
	if test -n "$userid"; then
		local kvsdir=$(flux job id --to=kvs $jobid)
		flux kvs get --raw ${kvsdir}.eventlog \
			| sed -e 's/\("userid":\)-*[0-9]*/\1'${userid}/ \
			| flux kvs put --raw ${kvsdir}.eventlog=-
	fi
}

# Usage: submit_job [userid]
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.  Optionally, edit the
# userid
submit_job() {
	local jobid=$(flux job submit sleeplong.json) &&
	flux job wait-event $jobid start >/dev/null &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean >/dev/null &&
	update_job_userid $1 &&
	echo $jobid
}

# Unlike above, do not cancel the job, the test will cancel the job
submit_job_live() {
	local jobid=$(flux job submit sleeplong.json) &&
	flux job wait-event $jobid start >/dev/null &&
	update_job_userid $1 &&
	echo $jobid
}

# Usage: bad_first_event
# Wait for job eventlog to include 'depend' event, then mangle submit event name
bad_first_event() {
	local jobid=$(flux job submit sleeplong.json) &&
	flux job wait-event $jobid depend >/dev/null &&
	local kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs get --raw ${kvsdir}.eventlog \
		| sed -e s/submit/foobar/ \
		| flux kvs put --raw ${kvsdir}.eventlog=- &&
	flux cancel $jobid &&
	echo $jobid
}

set_userid() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
	unset FLUX_HANDLE_USERID
	unset FLUX_HANDLE_ROLEMASK
}

test_expect_success 'job-info: generate jobspec for simple test job' '
	flux run --dry-run -n1 -N1 sleep 300 > sleeplong.json
'

#
# job lookup (via eventlog)
#

test_expect_success 'flux job eventlog works (owner)' '
	jobid=$(submit_job) &&
	flux job eventlog $jobid
'

test_expect_success 'flux job eventlog works (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	flux job eventlog $jobid &&
	unset_userid
'

test_expect_success 'flux job eventlog fails (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job eventlog $jobid &&
	unset_userid
'

test_expect_success 'flux job eventlog fails on bad first event (user)' '
	jobid=$(bad_first_event 9000) &&
	set_userid 9999 &&
	! flux job eventlog $jobid &&
	unset_userid
'

test_expect_success 'flux job guest.exec.eventlog works via -p (owner)' '
	jobid=$(submit_job) &&
	flux job eventlog -p exec $jobid
'

test_expect_success 'flux job guest.exec.eventlog works via -p (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	flux job eventlog -p exec $jobid &&
	unset_userid
'

test_expect_success 'flux job guest.exec.eventlog fails via -p (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job eventlog -p exec $jobid &&
	unset_userid
'

#
# job info lookup (via 'info')
#

test_expect_success 'flux job info eventlog works (owner)' '
	jobid=$(submit_job) &&
	flux job info $jobid eventlog
'

test_expect_success 'flux job info eventlog works (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	flux job info $jobid eventlog &&
	unset_userid
'

test_expect_success 'flux job info eventlog fails (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job info $jobid eventlog &&
	unset_userid
'

test_expect_success 'flux job info jobspec works (owner)' '
	jobid=$(submit_job) &&
	flux job info $jobid jobspec
'

test_expect_success 'flux job info jobspec works (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	flux job info $jobid jobspec &&
	unset_userid
'

test_expect_success 'flux job info jobspec fails (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job info $jobid jobspec &&
	unset_userid
'

test_expect_success 'flux job info foobar fails (owner)' '
	jobid=$(submit_job) &&
	! flux job info $jobid foobar
'

test_expect_success 'flux job info foobar fails (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	! flux job info $jobid foobar &&
	unset_userid
'

test_expect_success 'flux job info foobar fails (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job info $jobid foobar &&
	unset_userid
'

#
# job eventlog watch (via wait-event)
#

test_expect_success 'flux job wait-event works (owner)' '
	jobid=$(submit_job) &&
	flux job wait-event $jobid submit
'

test_expect_success 'flux job wait-event works (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	flux job wait-event $jobid submit &&
	unset_userid
'

test_expect_success 'flux job wait-event fails (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job wait-event $jobid submit &&
	unset_userid
'

test_expect_success 'flux job wait-event guest.exec.eventlog works via -p (owner)' '
	jobid=$(submit_job) &&
	flux job wait-event -p exec $jobid done
'

test_expect_success 'flux job wait-event guest.exec.eventlog works via -p (user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9000 &&
	flux job wait-event -p exec $jobid done &&
	unset_userid
'

test_expect_success 'flux job wait-event guest.exec.eventlog fails via -p (wrong user)' '
	jobid=$(submit_job 9000) &&
	set_userid 9999 &&
	! flux job wait-event -p exec $jobid done &&
	unset_userid
'

test_expect_success 'flux job wait-event guest.exec.eventlog works via -p (live job, owner)' '
	jobid=$(submit_job_live) &&
	flux job wait-event -p exec $jobid init &&
	flux cancel $jobid
'

test_expect_success 'flux job wait-event guest.exec.eventlog works via -p (live job, user)' '
	jobid=$(submit_job_live 9000) &&
	set_userid 9000 &&
	flux job wait-event -p exec $jobid init &&
	unset_userid &&
	flux cancel $jobid
'

test_expect_success 'flux job wait-event guest.exec.eventlog fails via -p (live job, wrong user)' '
	jobid=$(submit_job_live 9000) &&
	set_userid 9999 &&
	! flux job wait-event -p exec $jobid init &&
	unset_userid &&
	flux cancel $jobid
'

#
# job info invalid eventlog formatting corner case coverage
#

# for these tests we need to create a fake job eventlog in the KVS

# value of "dummy" irrelevant here, just need to lookup something

test_expect_success 'create empty eventlog for job' '
	jobpath=`flux job id --to=kvs 123456789` &&
	flux kvs put "${jobpath}.eventlog"="" &&
	flux kvs put "${jobpath}.dummy"="foobar"
'

test_expect_success 'flux job info dummy works (owner)' '
	flux job info $jobpath dummy
'

test_expect_success 'flux job info dummy fails (user)' '
	set_userid 9000 &&
        flux job info 123456789 dummy 2>&1 | grep "error parsing eventlog" &&
	unset_userid
'

test_expect_success 'create eventlog with invalid data / not JSON' '
	jobpath=`flux job id --to=kvs 123456789`&&
	flux kvs put "${jobpath}.eventlog"="foobar"
'

test_expect_success 'flux job info dummy fails (user)' '
	set_userid 9000 &&
        flux job info 123456789 dummy 2>&1 | grep "error parsing eventlog" &&
	unset_userid
'

test_expect_success 'create eventlog without submit context' '
        submitstr="{\"timestamp\":123.4,\"name\":\"submit\"}" &&
	jobpath=`flux job id --to=kvs 123456789` &&
	echo $submitstr | flux kvs put --raw "${jobpath}.eventlog"=-
'

test_expect_success 'flux job info dummy fails (user)' '
	set_userid 9000 &&
        flux job info 123456789 dummy 2>&1 | grep "error parsing eventlog" &&
	unset_userid
'

test_expect_success 'create eventlog without submit userid' '
        submitstr="{\"timestamp\":123.4,\"name\":\"submit\",\"context\":{}}" &&
	jobpath=`flux job id --to=kvs 123456789` &&
	echo $submitstr | flux kvs put --raw "${jobpath}.eventlog"=-
'

test_expect_success 'flux job info dummy fails (user)' '
	set_userid 9000 &&
        flux job info 123456789 dummy 2>&1 | grep "error parsing eventlog" &&
	unset_userid
'

test_expect_success 'create eventlog that is binary garbage' '
	jobpath=`flux job id --to=kvs 123456789` &&
        dd if=/dev/urandom bs=64 count=1 > binary.out &&
	flux kvs put --raw "${jobpath}.eventlog"=- < binary.out
'

test_expect_success 'flux job info dummy fails (user)' '
	set_userid 9000 &&
        flux job info 123456789 dummy 2>&1 | grep "error parsing eventlog" &&
	unset_userid
'

test_done
