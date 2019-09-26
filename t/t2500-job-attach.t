#!/bin/sh

test_description='Test flux job attach'

. $(dirname $0)/sharness.sh

test_under_flux 4

flux setattr log-stderr-level 1

test_expect_success 'attach: submit one job' '
	flux jobspec srun -n1 echo foo | flux job submit >jobid1
'

test_expect_success 'attach: job ran successfully' '
	run_timeout 5 flux job attach $(cat jobid1)
'

test_expect_success 'attach: --show-events shows clean event' '
	run_timeout 5 flux job attach \
		--show-event $(cat jobid1) 2>jobid1.events &&
	grep clean jobid1.events
'
test_expect_success 'attach: --show-events shows done event' '
	run_timeout 5 flux job attach \
		--show-exec $(cat jobid1) 2>jobid1.exec &&
	grep done jobid1.exec
'

test_expect_success 'attach: shows output from job' '
	run_timeout 5 flux job attach $(cat jobid1) | grep foo
'

test_expect_success 'attach: submit a job and cancel it' '
	flux jobspec srun -n1 sleep 30 | flux job submit >jobid2 &&
	flux job cancel $(cat jobid2)
'

test_expect_success 'attach: exit code reflects cancellation' '
	! flux job attach $(cat jobid2)
'

# Usage run_attach seq
# Run a 30s job, then attach to it in the background
# Write attach pid to pid${seq}.
# Write jobid to jobid${seq}
# Write attach stderr to attach${seq}.err (and poll until non-empty)
run_attach() {
	local seq=$1

	flux jobspec srun -n1 sleep 30 | flux job submit >jobid${seq}
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
	flux job cancel $(cat jobid4)
'

# Make sure live output occurs by seeing output "before" sleep, but no
# data "after" a sleep.
#
# To deal with racyness, script will output an event, which we can
# wait on
test_expect_success NO_CHAIN_LINT 'attach: output appears before cancel' '
        script=$SHARNESS_TEST_SRCDIR/job-attach/outputsleep.sh &&
	jobid=$(flux jobspec srun -n1 ${script} | flux job submit)
	flux job attach -E ${jobid} 1>attach5.out 2>attach5.err &
        waitpid=$! &&
        flux job wait-event --timeout=10.0 -p guest.exec.eventlog ${jobid} test-output-ready &&
        flux job cancel ${jobid} &&
	! wait ${waitpid} &&
        grep before attach5.out &&
        ! grep after attach5.out
'

test_done
