#!/bin/sh

test_description='Test flux shutdown command'

. $(dirname $0)/sharness.sh

test_under_flux 4

waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

export FLUX_URI_RESOLVE_LOCAL=t

test_expect_success 'flux-shutdown -h prints custom usage' '
	flux shutdown -h 2>usage &&
	grep "Usage:.*TARGET" usage
'
test_expect_success 'flux-shutdown fails on unknown option' '
	test_must_fail flux shutdown --notopt 2>notopt.err &&
	grep "unrecognized option" notopt.err
'
test_expect_success 'flux-shutdown fails if FLUX_URI is set wrong' '
	(FLUX_URI=noturi test_must_fail flux shutdown 2>baduri.err) &&
	grep "connecting to Flux" baduri.err
'
test_expect_success 'flux-shutdown fails if job argument is not a valid URI' '
	test_must_fail flux shutdown baduri 2>baduri2.err &&
	test_debug "cat baduri2.err" &&
	grep "failed to resolve" baduri2.err
'
test_expect_success 'flux-shutdown fails if job argument is unknown' '
	test_must_fail flux shutdown 12345 2>unkjobid.err &&
	grep "jobid 12345 not found" unkjobid.err
'

test_expect_success 'run a test job to completion' '
	flux mini submit --wait -n1 flux start /bin/true >jobid
'
test_expect_success 'flux-shutdown fails if job is not running' '
	test_must_fail flux shutdown $(cat jobid) 2>notrun.err &&
	test_debug "cat notrun.err" &&
	grep "jobid $(cat jobid) is not running" notrun.err
'

test_expect_success 'submit batch script and wait for it to start' '
	cat >batch.sh <<-EOT &&
	#!/bin/sh
	touch job2-has-started
	flux mini run sleep 300
	EOT
	chmod +x batch.sh &&
	flux mini batch -t30m -n1 batch.sh >jobid2 &&
	$waitfile job2-has-started
'
test_expect_success 'flux-shutdown JOBID works' '
	flux shutdown $(cat jobid2) 2>batch.err
'
test_expect_success 'shutdown output contains rc3 exit code (0)' '
	grep "rc3.*Exited (rc=0)" batch.err
'
test_expect_success 'shutdown output contains state transition to goodbye' '
	grep "rc3-success: finalize->goodbye" batch.err
'
test_expect_success 'job exit code indicates SIGHUP termination' '
	test_expect_code 129 flux job status $(cat jobid2)
'

test_expect_success 'submit non-batch job and wait for it to start' '
	flux mini submit -n1 \
		bash -c "touch job3-has-started && sleep 300" >jobid3 &&
	$waitfile job3-has-started
'
test_expect_success 'flux-shutdown JOBID fails when JOBID is not a flux instance' '
	test_must_fail flux shutdown $(cat jobid3) 2>notflux.err &&
	test_debug "cat notflux.err" &&
	grep "URI not found" notflux.err
'
test_expect_success 'cancel that job' '
	flux job cancel $(cat jobid3)
'

test_expect_success 'run instance with no initial program and wait for it to start' '
	flux mini submit --wait-event=start \
		flux start -o,-Sbroker.rc2_none >jobid3 &&
	run_timeout 30 bash -c "while ! flux uri $(cat jobid3) >uri3; do \
		sleep 0.1; \
	done"
'
# Retries required here because shutdown fails if instance is not yet
# in RUN state
test_expect_success 'flux-shutdown --quiet works' '
	while ! flux shutdown --quiet $(cat uri3) 2>shut3.err \
		&& grep "cannot be initiated in state" shut3.err; do \
		: ; \
	done
'
test_expect_success 'successful shutdown output is empty' '
	count=$(wc -l <shut3.err) &&
	test $count -eq 0
'

test_expect_success 'run batch job and wait for it to start' '
	cat >batch4.sh <<-EOT &&
	#!/bin/sh
	touch job4-has-started
	flux mini run sleep 300
	EOT
	chmod +x batch4.sh &&
	flux mini batch -n1 batch4.sh >jobid4 &&
	$waitfile job4-has-started
'
test_expect_success 'flux-shutdown --verbose works' '
	flux shutdown --verbose $(cat jobid4) 2>shut4.err
'
test_expect_success 'shutdown output contains debug log messages' '
	grep -q "debug\[0\]:" shut4.err
'

test_expect_success 'run multi-node batch job and wait for it to start' '
	cat >batch5.sh <<-EOT &&
	#!/bin/sh
	touch job5-has-started
	flux mini run sleep 300
	EOT
	chmod +x batch5.sh &&
	flux mini batch -N2 batch5.sh >jobid5 &&
	$waitfile job5-has-started
'
test_expect_success 'flux-shutdown --background works' '
	flux shutdown --background $(cat jobid5) 2>shut5.err
'
test_expect_success 'job exit code indicates SIGHUP termination' '
	test_expect_code 129 flux job status $(cat jobid5)
'

test_expect_success 'flux-shutdown as initial program does not hang' '
	test_expect_code 129 run_timeout 30 flux start flux shutdown
'

test_done
