#!/bin/sh

test_description='Test flux job attach'

. $(dirname $0)/sharness.sh

test_under_flux 4 full -Slog-stderr-level=1

runpty=$SHARNESS_TEST_SRCDIR/scripts/runpty.py
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

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
test_expect_success NO_CHAIN_LINT 'attach: --show-status notes stopped queue' '
	flux queue stop &&
	test_when_finished "flux queue start" &&
	jobid=$(flux submit hostname) &&
	$runpty -f asciicast -o stopped-queue.out \
		flux job attach --show-status $jobid &
	waitfile.lua -v -t 15 -p "default queue stopped" stopped-queue.out &&
	flux queue start &&
	wait
'
test_expect_success NO_CHAIN_LINT 'attach: --show-status notes stopped named queue' '
	flux config load <<-EOF &&
	[queues.batch]
	[queues.debug]
	EOF
	flux queue stop --verbose --all &&
	jobid=$(flux submit -qbatch hostname) &&
	$runpty -f asciicast -o stopped-batch.out \
		flux job attach --show-status $jobid &
	waitfile.lua -v -t 15 -p "batch queue stopped" stopped-batch.out &&
	flux queue start --all &&
	wait &&
	flux config load </dev/null &&
	flux queue status
'
test_expect_success NO_CHAIN_LINT 'attach: ignores non-fatal exceptions' '
	flux queue stop &&
	test_when_finished "flux queue start" &&
	jobid=$(flux submit hostname) &&
	$runpty -f asciicast -o status-exception.out \
		flux job attach --show-status $jobid &
	waitfile.lua -v -t 15 -p "waiting for resources" status-exception.out &&
	flux job raise --severity=2 --type=test -m test $(flux job last) &&
	flux queue start &&
	wait &&
	test_must_fail grep "canceling due to exception" status-exception.out &&
	grep "job.exception" status-exception.out
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
	id=$(flux submit sh -c "exit 42") &&
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
	id=$(flux submit sh -c "kill -9 \$PPID") &&
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

test_expect_success 'attach: SIGTERM is forwarded to job' '
	run_attach 5 &&
	pid=$(cat pid5) &&
	kill -TERM $pid &&
	test_must_fail_or_be_terminated wait $pid &&
	grep Terminated attach5.err
'
test_expect_success 'attach: SIGUSR1 is forwarded to job' '
	run_attach 6 &&
	pid=$(cat pid6) &&
	kill -USR1 $pid &&
	! wait $pid &&
	grep "User defined"  attach6.err
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
#
# --tail tests
#
test_expect_success 'attach: --tail fails with invalid arg' '
	id=`flux submit --wait hostname` &&
	test_must_fail flux job attach --tail=-1 ${id}
'
test_expect_success 'create big output test script' '
	cat <<-EOF >bigoutput.sh &&
	#!/bin/sh
	echo "START"
	s="$(seq -s " " 1 50)"
	for i in \$s
	do
		echo "FOO"
	done
	EOF
	chmod +x bigoutput.sh
'
test_expect_success 'attach: --tail works without arg (finished job)' '
	id=`flux submit --wait ./bigoutput.sh` &&
	flux job attach ${id} > tail1A.out &&
	head -n 1 tail1A.out | grep "START" &&
	count=`cat tail1A.out | wc -l` &&
	test $count -eq 51 &&
	flux job attach --tail ${id} > tail1B.out &&
	head -n 1 tail1B.out | grep "FOO" &&
	count=`cat tail1B.out | wc -l` &&
	test $count -eq 10
'
test_expect_success 'attach: --tail works with arg > 0 (finished job)' '
	id=`flux submit --wait ./bigoutput.sh` &&
	flux job attach ${id} > tail2A.out &&
	head -n 1 tail2A.out | grep "START" &&
	count=`cat tail2A.out | wc -l` &&
	test $count -eq 51 &&
	flux job attach --tail=20 ${id} > tail2B.out &&
	head -n 1 tail2B.out | grep "FOO" &&
	count=`cat tail2B.out | wc -l` &&
	test $count -eq 20
'
test_expect_success 'attach: --tail works with arg == 0 (finished job)' '
	id=`flux submit --wait ./bigoutput.sh` &&
	flux job attach ${id} > tail3A.out &&
	head -n 1 tail3A.out | grep "START" &&
	count=`cat tail3A.out | wc -l` &&
	test $count -eq 51 &&
	flux job attach --tail=0 ${id} > tail3B.out &&
	count=`cat tail3B.out | wc -l` &&
	test $count -eq 0
'
# N.B. output large amount of data immediately so we have something,
# then slowly output more to keep script going for awhile
test_expect_success 'create big output test script that stays alive' '
	cat <<-EOF >bigaliveoutput.sh &&
	#!/bin/sh
	echo "START"
	s="$(seq -s " " 1 100)"
	for i in \$s
	do
		echo "FOO"
	done
	s="$(seq -s " " 1 500)"
	for i in \$s
	do
		echo "BAR"
		sleep 0.2
	done
	EOF
	chmod +x bigaliveoutput.sh
'
# live job test notes
# - To avoid a race, must make sure output from "bigaliveoutput.sh" is
#   in the KVS and ready to be read before testing `--tail`.  Due to
#   issue #6896 we watch the output via "flux job eventlog", rather
#   than using "flux job attach".
# - recall two INT signals will detach "flux job attach" and cancel job
test_expect_success NO_CHAIN_LINT 'attach: --tail works without arg (live job)' '
	id=`flux submit -t100s ./bigaliveoutput.sh`

	flux job eventlog -p guest.output --follow ${id} > tail4A.out &
	pidA=$!
	$waitfile -t 30 -p BAR tail4A.out

	flux job attach --tail ${id} > tail4B.out &
	pidB=$! &&
	$waitfile -t 30 -p BAR tail4B.out &&
	kill -INT ${pidB} &&
	sleep 0.2 &&
	kill -INT ${pidB} &&
	! wait ${pidA} &&
	! wait ${pidB} &&
	test_must_fail grep "START" tail4B.out &&
	tail -n 1 tail4B.out | grep "BAR" &&
	count=`grep FOO tail4B.out | wc -l` &&
	test $count -lt 10
'
test_expect_success NO_CHAIN_LINT 'attach: --tail works with arg > 0 (live job)' '
	id=`flux submit -t100s ./bigaliveoutput.sh`

	flux job eventlog -p guest.output --follow ${id} > tail5A.out &
	pidA=$!
	$waitfile -t 30 -p BAR tail5A.out

	flux job attach --tail=20 ${id} > tail5B.out &
	pidB=$! &&
	$waitfile -t 30 -p BAR tail5B.out &&
	kill -INT ${pidB} &&
	sleep 0.2 &&
	kill -INT ${pidB} &&
	! wait ${pidA} &&
	! wait ${pidB} &&
	test_must_fail grep "START" tail5B.out &&
	tail -n 1 tail5B.out | grep "BAR" &&
	count=`grep FOO tail4B.out | wc -l` &&
	test $count -lt 20
'
test_expect_success NO_CHAIN_LINT 'attach: --tail works with arg == 0 (live job)' '
	id=`flux submit -t100s ./bigaliveoutput.sh`

	flux job eventlog -p guest.output --follow ${id} > tail6A.out &
	pidA=$!
	$waitfile -t 30 -p BAR tail6A.out

	flux job attach --tail=0 ${id} > tail6B.out &
	pidB=$! &&
	$waitfile -t 30 -p BAR tail6B.out &&
	kill -INT ${pidB} &&
	sleep 0.2 &&
	kill -INT ${pidB} &&
	! wait ${pidA} &&
	! wait ${pidB} &&
	test_must_fail grep "START" tail6B.out &&
	tail -n 1 tail6B.out | grep "BAR" &&
	test_must_fail grep "FOO" tail6B.out
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
test_expect_success 'attach: EPERM generates sensible error' '
	jobid=$(flux job last) &&
	( export FLUX_HANDLE_USERID=42 &&
	  export FLUX_HANDLE_ROLEMASK=0x2 &&
	  test_must_fail flux job attach -vEX $jobid 2>eperm.err
	) &&
	test_debug "cat eperm.err" &&
	grep -i "not your job" eperm.err
'
test_done
