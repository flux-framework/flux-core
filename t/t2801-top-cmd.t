#!/bin/sh

test_description='Test flux top command'

. $(dirname $0)/sharness.sh

test_under_flux 4

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
testssh="${SHARNESS_TEST_SRCDIR}/scripts/tssh"

export FLUX_URI_RESOLVE_LOCAL=t

test_expect_success 'flux-top -h prints custom usage' '
	flux top -h 2>usage &&
	grep "Usage:.*TARGET" usage
'
test_expect_success 'flux-top fails on unknown option' '
	test_must_fail flux top --notopt 2>notopt.err &&
	grep "unrecognized option" notopt.err
'
test_expect_success 'flux-top fails if FLUX_URI is set wrong' '
	(FLUX_URI=noturi test_must_fail $runpty -n --stderr=baduri.err flux top) &&
	grep "connecting to Flux" baduri.err
'
test_expect_success 'flux-top fails if job argument is not a valid URI' '
	test_must_fail $runpty -n --stderr=baduri.err flux top baduri &&
	test_debug "cat baduri.err" &&
	grep "failed to resolve" baduri.err
'
test_expect_success 'flux-top fails if job argument is unknown' '
	test_must_fail $runpty -n --stderr=unkjobid.err flux top 12345 &&
	grep "jobid 12345 not found" unkjobid.err
'
test_expect_success 'run a test job to completion' '
	flux mini submit --wait -n1 flux start /bin/true >jobid
'
test_expect_success 'flux-top fails if job is not running' '
	test_must_fail \
		$runpty -n --stderr=notrun.err flux top $(cat jobid) &&
	test_debug "cat notrun.err" &&
	grep "jobid $(cat jobid) is not running" notrun.err
'
test_expect_success 'flux-top fails if stdin is not a tty' '
	test_must_fail flux top --test-exit </dev/null 2>notty.err &&
	grep "stdin is not a terminal" notty.err
'
test_expect_success 'flux-top fails if TERM is not supported' '
	test_must_fail $runpty --term=dumb --stderr=dumb.err flux top &&
	grep "terminal does not support required capabilities" dumb.err
'
test_expect_success 'flux-top --test-exit works with a pty' '
	$runpty flux top --test-exit >/dev/null
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
test_expect_success 'flux-top JOBID works' '
	FLUX_SSH=$testssh $runpty --format=asciicast -o topjob.log \
		flux top --test-exit $(cat jobid2)
'
test_expect_success 'submit non-batch job and wait for it to start' '
	flux mini submit -n1 \
		bash -c "touch job3-has-started && sleep 300" >jobid3 &&
	$waitfile job3-has-started
'
test_expect_success 'flux-top JOBID fails when JOBID is not a flux instance' '
	FLUX_SSH=$testssh test_must_fail \
		$runpty --format=asciicast -o notflux.log \
		flux top --test-exit $(cat jobid3) &&
	test_debug "cat notflux.log" &&
	grep "URI not found" notflux.log
'
test_expect_success NO_CHAIN_LINT 'flux-top quits on q keypress' '
	$runpty --quit-char=q --format=asciicast -o keys.log flux top &
	pid=$! &&
	sleep 1 &&
	kill -USR1 $pid &&
	wait $pid
'
test_expect_success NO_CHAIN_LINT 'flux-top can call itself recursively' '
	SHELL=/bin/sh &&
	flux jobs &&
	flux proxy $(cat jobid2) flux jobs -c1 -no {id} >expected.id &&
	cat <<-EOF >recurse.in &&
	{ "version": 2 }
	[0.5, "i", "j"]
	[0.55, "i", "j"]
	[0.60, "i", "j"]
	[0.65, "i", "k"]
	[0.70, "i", "\n"]
	[1.00, "i", "q"]
	[1.10, "i", "q"]
	EOF
	$runpty -o recurse.log --input=recurse.in flux top &&
	grep -q $(echo $(cat expected.id) | sed "s/ƒ//") recurse.log
'

test_done
