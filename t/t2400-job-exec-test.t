#!/bin/sh

test_description='Test flux job execution service in simulated mode'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

flux setattr log-stderr-level 1

RPC=${FLUX_BUILD_DIR}/t/request/rpc

job_kvsdir()    { flux job id --to=kvs $1; }
exec_eventlog() { flux kvs get -r $(job_kvsdir $1).guest.exec.eventlog; }

submit_as_alternate_user()
{
        FAKE_USERID=42
        flux run --dry-run "$@" | \
          flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
            >job.signed
        FLUX_HANDLE_USERID=$FAKE_USERID \
          flux job submit --flags=signed job.signed
}

test_expect_success 'job-exec: generate jobspec for simple test job' '
	flux run \
	    --setattr=system.exec.test.run_duration=0.0001s \
	    --dry-run hostname > basic.json
'
test_expect_success 'job-exec: basic job runs in simulated mode' '
	jobid=$(flux job submit basic.json) &&
	flux job wait-event -t 1 ${jobid} start &&
	flux job wait-event -t 1 ${jobid} finish &&
	flux job wait-event -t 1 ${jobid} release &&
	flux job wait-event -t 1 ${jobid} clean
'
test_expect_success 'job-exec: guestns linked into primary' '
	#  guest key is not a link
	test_must_fail \
	  flux kvs readlink $(job_kvsdir ${jobid}).guest 2>readlink.err &&
	grep "Invalid argument" readlink.err &&
	#  gues key is a directory
	test_must_fail \
	  flux kvs get $(job_kvsdir ${jobid}).guest 2>kvsdir.err &&
	grep "Is a directory" kvsdir.err
'
test_expect_success 'job-exec: exec.eventlog exists with expected states' '
	exec_eventlog ${jobid} > eventlog.1.out &&
	head -1 eventlog.1.out | grep "init" &&
	tail -1 eventlog.1.out | grep "done"
'
test_expect_success 'job-exec: canceling job during execution works' '
	jobid=$(flux submit \
                --setattr=system.exec.test.run_duration=10s hostname) &&
	flux job wait-event -vt 2.5 ${jobid} start &&
	flux cancel ${jobid} &&
	flux job wait-event -t 2.5 ${jobid} exception &&
	flux job wait-event -t 2.5 ${jobid} finish | grep status=15 &&
	flux job wait-event -t 2.5 ${jobid} release &&
	flux job wait-event -t 2.5 ${jobid} clean &&
	exec_eventlog $jobid | grep "complete" | grep "\"status\":15"
'
test_expect_success 'job-exec: mock exception during initialization' '
	jobid=$(flux submit \
	         --setattr=system.exec.test.mock_exception=init true) &&
	flux job wait-event -t 2.5 ${jobid} exception > exception.1.out &&
	test_debug "flux job eventlog ${jobid}" &&
	grep "type=\"exec\"" exception.1.out &&
	grep "mock initialization exception generated" exception.1.out &&
	flux job wait-event -qt 2.5 ${jobid} clean &&
	flux job eventlog ${jobid} > eventlog.${jobid}.out &&
	test_must_fail grep "finish" eventlog.${jobid}.out
'
test_expect_success 'job-exec: mock exception during run' '
	jobid=$(flux submit \
	         --setattr=system.exec.test.mock_exception=run true) &&
	flux job wait-event -t 2.5 ${jobid} exception > exception.2.out &&
	grep "type=\"exec\"" exception.2.out &&
	grep "mock run exception generated" exception.2.out &&
	flux job wait-event -qt 2.5 ${jobid} clean &&
	flux job eventlog ${jobid} > eventlog.${jobid}.out &&
	grep "finish status=15" eventlog.${jobid}.out
'
test_expect_success 'start request with empty payload fails with EPROTO(71)' '
	${RPC} job-exec.start 71 </dev/null
'
test_expect_success 'job-exec: invalid testexec conf generates exception' '
	jobid=$(flux submit \
	    --setattr=system.exec.test.run_duration=0.01 hostname) &&
	flux job wait-event -t 5 ${jobid} exception > except.invalid.out &&
	grep "type=\"exec\"" except.invalid.out &&
	flux job wait-event -qt 5 ${jobid} clean &&
	flux job eventlog ${jobid}
'
test_expect_success 'job-exec: test exec start override works' '
	jobid=$(flux submit \
	    --setattr=system.exec.test.override=1 \
	    --setattr=system.exec.test.run_duration=0.001s \
	    true) &&
	flux job wait-event -t 5 ${jobid} alloc &&
	test_must_fail flux job wait-event -t 0.1 ${jobid} start &&
	flux job-exec-override start ${jobid} &&
	flux job wait-event -t 5 ${jobid} start &&
	flux job wait-event -t 5 -v ${jobid} clean
'
test_expect_success 'job-exec: override only works on jobs with flag set' '
	jobid=$(flux submit \
		--setattr=system.exec.test.run_duration=0. /bin/true) &&
	flux job wait-event -t 5 ${jobid} alloc &&
	test_must_fail flux job-exec-override start ${jobid} &&
	flux cancel ${jobid} &&
	flux job wait-event -t 5 -v ${jobid} clean
'
test_expect_success 'job-exec: test exec start/finish override works' '
	jobid=$(flux submit \
	    --setattr=system.exec.test.override=1 \
	    true) &&
	flux job wait-event -t 5 ${jobid} alloc &&
	test_must_fail flux job wait-event -t 0.1 ${jobid} start &&
	test_must_fail flux job-exec-override finish ${jobid} 0 &&
	flux job-exec-override start ${jobid} &&
	flux job wait-event -t 5 ${jobid} start &&
	test_must_fail flux job-exec-override start ${jobid} &&
	test_must_fail flux job wait-event -t 0.1 ${jobid} finish &&
	flux job-exec-override finish ${jobid} 0 &&
	flux job wait-event -t 5 -v ${jobid} clean
'
test_expect_success 'job-exec: flux job-exec-override fails on invalid id' '
	test_must_fail flux job-exec-override start 1234  &&
	test_must_fail flux job-exec-override finish 1234  0
'
test_expect_success 'job-exec: job-exec.testoverride invalid request' '
	cat <<-EOF >override.py &&
	import json
	import sys
	import flux
	h = flux.Flux()
	try:
	    print(h.rpc("job-exec.override", json.load(sys.stdin)).get())
	except OSError as exc:
	    print(str(exc), file=sys.stderr)
	    sys.exit(1)
	EOF
	echo {} | \
	  test_must_fail flux python override.py &&
	jobid=$(flux submit \
	    --setattr=system.exec.test.override=1 \
	    true) &&
	cat <<-EOF >badevent.json &&
	{"jobid":"$(flux job id --to=dec ${jobid})", "event":"foo"}
	EOF
	test_must_fail flux python override.py < badevent.json &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success 'job-exec: flux job-exec-override fails for invalid userid' '
	jobid=$(flux submit \
	    --setattr=system.exec.test.override=1 \
	    true) &&
	newid=$(($(id -u)+1)) &&
	( export FLUX_HANDLE_ROLEMASK=0x2 &&
	  export FLUX_HANDLE_USERID=$newid &&
	    test_must_fail flux job-exec-override start ${jobid}
	) &&
	flux cancel ${jobid} &&
	flux job wait-event -t 5 -v ${jobid} clean
'
test_expect_success 'job-exec: critical-ranks RPC handles unexpected input' '
	cat <<-EOF >critical-ranks.py &&
	import sys
	import flux
	from flux.job import JobID
	h = flux.Flux()
	h.rpc("job-exec.critical-ranks",
	      {"id": JobID(sys.argv[1]), "ranks": sys.argv[2]}).get()
	EOF
	test_must_fail flux python critical-ranks.py 1234 0-1 &&
	id=$(flux submit --wait-event=start sleep 300) &&
	test_must_fail flux python critical-ranks.py $id foo &&
	newid=$(($(id -u)+1)) &&
        ( export FLUX_HANDLE_ROLEMASK=0x2 &&
          export FLUX_HANDLE_USERID=$newid &&
            test_must_fail flux python critical-ranks.py $id 0
        )
'
test_expect_success FLUX_SECURITY 'job-exec: guests denied access to test exec' '
	jobid=$(submit_as_alternate_user \
		--setattr=exec.test.run_duration=1s hostname) &&
	flux job wait-event -Hvt 15s $jobid exception &&
	flux job wait-event -Ht 15s $jobid exception >testexec-denied.out &&
	grep "guests may not use test" testexec-denied.out
'
test_expect_success FLUX_SECURITY \
	'job-exec: guest access to test exec can be configured' '
	flux config load <<-EOF &&
	[exec.testexec]
	allow-guests = true
	EOF
	jobid=$(submit_as_alternate_user \
		--setattr=exec.test.run_duration=0.1s hostname) &&
	flux job wait-event -vHt 15s $jobid clean &&
	flux job status -vvv $jobid
'

if ! grep 'release 7' /etc/centos-release >/dev/null 2>&1 \
   && ! grep 'release 7' /etc/redhat-release >/dev/null 2>&1
then
   test_set_prereq NOT_DISTRO7
fi

# The following test does not work on CentOS 7 / RHEL7 since exec errno does
#  not work with job-exec (for as yet unknown reason). Skip the test on
#  these distros:
test_expect_success NOT_DISTRO7 'job-exec: path to shell is emitted on exec error' '
	test_expect_code 127 flux run \
	  --setattr=exec.job_shell=/foo/flux-shell hostname 2>exec.err &&
	test_debug "cat exec.err" &&
	grep /foo/flux-shell exec.err
'
test_done
