#!/bin/sh

test_description='Test flux top command'

. $(dirname $0)/sharness.sh

test_under_flux 4

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
testssh="${SHARNESS_TEST_SRCDIR}/scripts/tssh"

test_expect_success 'flux-top -h prints custom usage' '
	flux top -h 2>usage &&
	grep "Usage:.*JOBID" usage
'
test_expect_success 'flux-top fails on unknown option' '
	test_must_fail flux top --notopt 2>notopt.err &&
	grep "unrecognized option" notopt.err
'
test_expect_success 'flux-top fails if FLUX_URI is set wrong' '
	(FLUX_URI=noturi test_must_fail flux top) 2>baduri.err &&
	grep "connecting to Flux" baduri.err
'
test_expect_success 'flux-top fails if job argument is not a job ID' '
	test_must_fail flux top notjob 2>badjobid.err &&
	grep "failed to parse JOBID" badjobid.err
'
test_expect_success 'flux-top fails if job argument is unknown' '
	test_must_fail flux top 12345 2>unkjobid.err &&
	grep "unknown job" unkjobid.err
'
test_expect_success 'run a test job to completion' '
	flux mini submit --wait -n1 /bin/true >jobid
'
test_expect_success 'flux-top fails if job is not running' '
	test_must_fail flux top $(cat jobid) 2>notrun.err &&
	grep "job is not running" notrun.err
'
test_expect_success 'flux-top fails if stdin is not a tty' '
	test_must_fail flux top --test-exit </dev/null 2>notty.err &&
	grep "stdin is not a terminal" notty.err
'
test_expect_success 'flux-top --test-exit works with a pty' '
	$runpty flux top --test-exit >/dev/null
'
test_expect_success 'submit batch script and wait for it to start' '
	cat >batch.sh <<-EOT &&
	#!/bin/sh
	touch job2-has-started
	sleep inf
	EOT
	chmod +x batch.sh &&
	flux mini batch -t30m -n1 batch.sh >jobid2 &&
	$waitfile job2-has-started
'
test_expect_success 'flux-top JOBID works' '
	FLUX_SSH=$testssh $runpty --format=asciicast -o topjob.log \
		flux top --test-exit $(cat jobid2)
'
test_expect_success 'submit non-batch job and wait for it to start' '
	flux mini submit -n1 \
		bash -c "touch job3-has-started && sleep inf" >jobid3 &&
	$waitfile job3-has-started
'
test_expect_success 'flux-top JOBID fails when JOBID is not a flux instance' '
	FLUX_SSH=$testssh test_must_fail \
		$runpty --format=asciicast -o notflux.log \
		flux top --test-exit $(cat jobid3) &&
	grep "not a Flux instance" notflux.log
'
test_expect_success NO_CHAIN_LINT 'flux-top quits on q keypress' '
	$runpty --quit-char=q --format=asciicast -o keys.log flux top &
	pid=$! &&
	sleep 1 &&
	kill -USR1 $pid &&
	wait $pid
'

test_done
