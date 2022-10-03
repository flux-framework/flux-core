#!/bin/sh

test_description='Test flux top command'

. $(dirname $0)/sharness.sh

mkdir -p conf.d

test_under_flux 4 full -o,--config-path=$(pwd)/conf.d

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
	[1.0, "i", "j"]
	[1.5, "i", "j"]
	[2.0, "i", "k"]
	[2.5, "i", "\n"]
	[3.25, "i", "q"]
	[3.75, "i", "q"]
	EOF
	$runpty -o recurse.log --input=recurse.in flux top &&
	grep -q $(echo $(cat expected.id) | sed "s/ƒ//") recurse.log
'
test_expect_success NO_CHAIN_LINT 'flux-top does not exit on recursive failure' '
	cat <<-EOF1 >ssh-fail &&
	#!/bin/sh
	printf "ssh failure\n" >&2
	EOF1
	chmod +x ssh-fail &&
	SHELL=/bin/sh &&
	flux jobs &&
	flux proxy $(cat jobid2) flux jobs -c1 -no {id} >expected.id &&
	cat <<-EOF >recurse-fail.in &&
	{ "version": 2 }
	[0.5, "i", "j"]
	[1.0, "i", "j"]
	[1.5, "i", "j"]
	[2.0, "i", "k"]
	[2.5, "i", "\n"]
	[3.25, "i", "x"]
	[3.75, "i", "q"]
	EOF
	unset FLUX_URI_RESOLVE_LOCAL &&
	FLUX_SSH=$(pwd)/ssh-fail \
	    $runpty -f asciicast -o recurse-fail.log \
	        --input=recurse-fail.in flux top &&
	grep -qi "error connecting to Flux" recurse-fail.log
'
test_expect_success 'configure a test queue' '
	cat >conf.d/config.toml <<-EOT &&
	[queues.testq]
	EOT
	flux config reload
'
test_expect_success 'flux-top displays job queues when present' '
	$runpty -f asciicast -o no-queue.log flux top --test-exit &&
	grep -v QUEUE no-queue.log &&
	jobid=$(flux mini submit --wait-event=start -q testq sleep 30) &&
	$runpty -f asciicast -o queue.log flux top --test-exit &&
	grep QUEUE queue.log &&
	grep testq queue.log &&
	flux job cancel $jobid &&
	flux job wait-event $jobid clean
'
test_done
