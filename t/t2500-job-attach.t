#!/bin/sh

test_description='Test flux job attach'

. $(dirname $0)/sharness.sh

test_under_flux 4

flux setattr log-stderr-level 1

runpty=$SHARNESS_TEST_SRCDIR/scripts/runpty.py

test_expect_success 'attach: submit one job' '
	flux submit echo foo >jobid1
'

test_expect_success 'attach: job ran successfully' '
	run_timeout 5 flux job attach $(cat jobid1)
'
test_expect_success 'attach: --show-events shows finish event' '
	run_timeout 5 flux job attach \
		--show-event $(cat jobid1) 2>jobid1.events &&
	grep finish jobid1.events
'
test_expect_success 'attach: --show-events -w clean shows clean event' '
	run_timeout 5 flux job attach \
		--show-event -w clean $(cat jobid1) 2>jobid1.events &&
	grep clean jobid1.events
'
test_expect_success 'attach: --show-events shows done event' '
	run_timeout 5 flux job attach \
		--show-exec $(cat jobid1) 2>jobid1.exec &&
	grep done jobid1.exec
'
test_expect_success 'attach: --show-status shows job status line' '
	run_timeout 5 flux job attach \
		--show-status $(cat jobid1) 2>jobid1.status &&
	grep "resolving dependencies" jobid1.status &&
	grep "waiting for resources" jobid1.status &&
	grep "starting" jobid1.status &&
	grep "started" jobid1.status
'
test_expect_success 'attach: no status is shown with FLUX_ATTACH_NONINTERACTIVE' '
	(export FLUX_ATTACH_NONINTERACTIVE=t &&
	 run_timeout 5 flux job attach \
		--show-status $(cat jobid1) 2>jobid1.status2
	) &&
	test_must_fail grep "resolving dependencies" jobid1.status2 &&
	test_must_fail grep "waiting for resources" jobid1.status2 &&
	test_must_fail grep "starting" jobid1.status2 &&
	test_must_fail grep "started" jobid1.status2

'
test_expect_success 'attach: --show-status properly accounts prolog-start events' '
	flux jobtap load ${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs/perilog-test.so prolog-count=4 &&
	jobid2=$(flux submit hostname) &&
	run_timeout 5 $runpty -f asciicast -o jobid2.out \
		flux job attach -vEX --show-status $jobid2 &&
	cat jobid2.out &&
	grep "resolving dependencies" jobid2.out &&
	grep "waiting for resources" jobid2.out &&
	grep "starting" jobid2.out &&
	grep "started" jobid2.out &&
	last_prolog_finish_line=$(grep -n prolog-finish jobid2.out \
		| tail -1 \
		| cut -f1 -d:) &&
	first_starting_line=$(grep -n flux-job:.*starting jobid2.out \
		| tail -1 \
		| cut -f1 -d:) &&
	test $first_starting_line -ge $last_prolog_finish_line &&
	flux job wait-event $jobid2 clean &&
	flux jobtap remove perilog-test.so
'
test_expect_success 'attach: shows output from job' '
	run_timeout 5 flux job attach $(cat jobid1) | grep foo
'

test_expect_success 'attach: submit a job and cancel it' '
	flux submit sleep 30 >jobid2 &&
	flux cancel $(cat jobid2)
'

test_expect_success 'attach: exit code reflects cancellation' '
	! flux job attach $(cat jobid2)
'
test_expect_success 'attach: reports task exit code with nonzero exit' '
	id=$(flux submit sh -c 'exit 42') &&
	test_must_fail flux job attach $id 2>exited.err &&
	test_debug "cat exited.err" &&
	grep "exited with exit code 42" exited.err
'
test_expect_success 'attach: reports Killed when job tasks are killed' '
	id=$(flux submit --wait-event=exec.shell.start sleep 30) &&
	flux job kill -s 9 $id &&
	test_must_fail_or_be_terminated flux job attach $id 2>killed.err &&
	test_debug "cat killed.err" &&
	grep Killed killed.err
'
test_expect_success 'attach: reports Terminated when tasks are terminated' '
	id=$(flux submit --wait-event=exec.shell.start sleep 30) &&
	flux job kill -s 15 $id &&
	test_must_fail_or_be_terminated flux job attach $id 2>terminated.err &&
	test_debug "cat terminated.err" &&
	grep Terminated terminated.err
'
test_expect_success 'attach: reports job shell Killed if job shell is killed' '
	id=$(flux submit --wait-event=exec.shell.start sh -c "kill -9 \$PPID") &&
	test_must_fail_or_be_terminated flux job attach $id 2>shell-killed.out &&
	grep "job shell Killed" shell-killed.out
'

# Usage run_attach seq
# Run a 30s job, then attach to it in the background
# Write attach pid to pid${seq}.
# Write jobid to jobid${seq}
# Write attach stderr to attach${seq}.err (and poll until non-empty)
run_attach() {
	local seq=$1

	flux submit sleep 30 >jobid${seq}
	flux job attach -E $(cat jobid${seq}) 2>attach${seq}.err &
	echo $! >pid${seq}
	while ! test -s attach${seq}.err; do sleep 0.1; done
}

test_expect_success 'attach: two SIGINTs cancel a job' '
	run_attach 3 &&
	pid=$(cat pid3) &&
	kill -INT $pid &&
	sleep 0.2 &&
	kill -INT $pid &&
	! wait $pid
'

test_expect_success 'attach: SIGINT+SIGTSTP detaches from job' '
	run_attach 4 &&
	pid=$(cat pid4) &&
	kill -INT $pid &&
	sleep 0.2 &&
	kill -TSTP $pid &&
	test_must_fail wait $pid
'

test_expect_success 'attach: detached job was not canceled' '
	flux job eventlog $(cat jobid4) >events4 &&
	test_must_fail grep -q cancel events4 &&
	flux cancel $(cat jobid4)
'

# Make sure live output occurs by seeing output "before" sleep, but no
# data "after" a sleep.
#
# To deal with racyness, script will output an event, which we can
# wait on
test_expect_success NO_CHAIN_LINT 'attach: output appears before cancel' '
	script=$SHARNESS_TEST_SRCDIR/job-attach/outputsleep.sh &&
	jobid=$(flux submit ${script})
	flux job attach -E ${jobid} 1>attach5.out 2>attach5.err &
	waitpid=$! &&
	flux job wait-event --timeout=10.0 -p exec ${jobid} test-output-ready &&
	flux cancel ${jobid} &&
	! wait ${waitpid} &&
	grep before attach5.out &&
	! grep after attach5.out
'

test_expect_success 'attach: output events processed after shell.init failure' '
	jobid=$(flux submit -o initrc=noinitrc hostname) &&
	flux job wait-event -v ${jobid} clean &&
	flux job eventlog -p guest.output ${jobid} &&
	(flux job attach ${jobid} >init-failure.output 2>&1 || true) &&
	test_debug "cat init-failure.output" &&
	grep "FATAL:.*noinitrc: No such file or directory" init-failure.output
'

# use a shell function to make sane quoting possible
filter_log_context() {
	jq -c '. | select(.name == "log") | .context'
}

test_expect_success 'attach: -v option displays file and line info in logs' '
	jobid=$(flux submit -o verbose=2 hostname) &&
	flux job wait-event ${jobid} clean &&
	flux job eventlog --format=json -p guest.output ${jobid} \
		| filter_log_context >verbose.json &&
	file=$(head -1 verbose.json | jq -r .file) &&
	line=$(head -1 verbose.json | jq -r .line) &&
	msg=$(head -1 verbose.json | jq -r .message) &&
	flux job attach -v $jobid >verbose.output 2>&1 &&
	grep "$file:$line: $message" verbose.output
'
test_expect_success 'attach: cannot attach to interactive pty when --read-only specified' '
	jobid=$(flux submit -o pty.interactive cat) &&
	test_must_fail flux job attach --read-only $jobid &&
	$SHARNESS_TEST_SRCDIR/scripts/runpty.py -i none -f asciicast -c q \
		flux job attach $jobid
'
test_expect_success 'attach: --stdin-ranks works' '
	id=$(flux submit -N4 -o exit-timeout=none -t60s cat) &&
	echo hello from 0 \
		| flux job attach -vEX --label-io -i0 $id >stdin-ranks.out &&
	flux job eventlog -p guest.input $id &&
	cat <<-EOF >stdin-ranks.expected &&
	0: hello from 0
	EOF
	test_cmp stdin-ranks.expected stdin-ranks.out
'
test_expect_success 'attach: --stdin-ranks with invalid idset errors' '
	id=$(flux submit -t60s cat) &&
	test_must_fail flux job attach -i 5-0 $id &&
	flux cancel $id
'
test_expect_success 'attach: --stdin-ranks is adjusted to intersection' '
	id=$(flux submit -n2 -t60s cat) &&
	echo foo | flux job attach --label-io -i1-2 $id >adjusted.out 2>&1 &&
	test_debug "cat adjusted.out" &&
	grep "warning: adjusting --stdin-ranks" adjusted.out
'
test_expect_success 'attach: --stdin-ranks cannot be used with --read-only' '
	id=$(flux submit -n2 -t60s cat) &&
	test_must_fail flux job attach -i all --read-only $id &&
	flux cancel $id
'
jobpipe=$SHARNESS_TEST_SRCDIR/scripts/pipe.py
test_expect_success 'attach: writing to stdin of closed tasks returns EPIPE' '
	id=$(flux submit -N4 -t60s cat) &&
	$jobpipe $id 0 </dev/null &&
	test_must_fail $jobpipe $id 0 </dev/null >pipe.out 2>&1 &&
	$jobpipe $id all </dev/null &&
	test_debug "cat pipe.out" &&
	grep -i "Broken pipe" pipe.out
'
test_done
