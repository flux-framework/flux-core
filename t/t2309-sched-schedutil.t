#!/bin/sh

test_description='Test the libschedutil convenience library

Drive libschedutil through a minimal test scheduler (schedutil-shim) that
does no real scheduling.  The shim grants a canned R controlled by KVS
keys, so these tests exercise the library alloc/deny/cancel response paths
and confirm what libschedutil commits to the KVS and returns to the
job-manager.
'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 2 job

SHIM=${FLUX_BUILD_DIR}/t/sched/.libs/schedutil-shim.so

# The shim grants a fixed two-rank R to every job it allocates.
flux R encode -r0-1 -c0-1 >shim_R.json

# True once housekeeping for job $1 has released one rank back to the
# scheduler and holds exactly one straggler (pending and allocated both "1").
one_rank_released () {
	flux module stats job-manager \
		| jq -e ".housekeeping.running[\"$1\"]
		         | .pending == \"1\" and .allocated == \"1\"" \
		>/dev/null 2>&1
}

test_expect_success 'unload sched-simple and job-exec' '
	flux module remove sched-simple &&
	flux module remove job-exec
'
test_expect_success 'reload ingest without validator' '
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'configure shim to grant canned R' '
	flux kvs put test.schedutil.mode=success &&
	flux kvs put test.schedutil.R="$(cat shim_R.json)"
'
test_expect_success 'load schedutil-shim as the scheduler' '
	flux module load ${SHIM}
'
test_expect_success 'alloc: job receives an alloc event' '
	jobid=$(flux submit hostname) &&
	flux job wait-event -t 30 $jobid alloc
'
test_expect_success 'alloc: libschedutil committed R to the KVS' '
	flux job info $jobid R >alloc_R.json &&
	test_debug "jq -S . <alloc_R.json" &&
	jq -e ".execution.R_lite[0].rank == \"0-1\"" <alloc_R.json
'
test_expect_success 'alloc: success response carries scheduler annotation' '
	test "$(flux jobs -no {annotations.sched.resource_summary} $jobid)" \
		= "rank[0-1]"
'
test_expect_success 'alloc: grant R with hardware and instance-local props' '
	flux R encode -r0-1 -c0-1 \
		| flux R set-property xx:0-1 \
		| flux R set-property +batch:0-1 >prop_R.json &&
	flux kvs put test.schedutil.R="$(cat prop_R.json)"
'
test_expect_success "alloc: libschedutil strips '+' properties from job R" '
	jobid=$(flux submit hostname) &&
	flux job wait-event -t 30 $jobid alloc &&
	flux job info $jobid R >stripped_R.json &&
	test_debug "jq -S .execution.properties <stripped_R.json" &&
	jq -e ".execution.properties.xx == \"0-1\"" <stripped_R.json &&
	test_must_fail jq -e ".execution.properties[\"+batch\"]" <stripped_R.json
'
test_expect_success 'alloc: restore canned R without properties' '
	flux kvs put test.schedutil.R="$(cat shim_R.json)"
'
test_expect_success 'deny: unschedulable jobs receive an exception' '
	flux kvs put test.schedutil.mode=deny &&
	jobid=$(flux submit hostname) &&
	flux job wait-event -t 30 $jobid exception &&
	flux job eventlog $jobid | grep -i "denied for test"
'
test_expect_success 'annotate: pending job receives an annotation update' '
	flux kvs put test.schedutil.mode=hold &&
	jobid=$(flux submit --flags=debug hostname) &&
	flux job wait-event -t 30 $jobid debug.alloc-request &&
	test "$(flux jobs -no {annotations.sched.reason_pending} $jobid)" \
		= "held for test"
'
test_expect_success 'cancel: a held alloc request can be canceled' '
	flux cancel $jobid &&
	flux job wait-event -t 30 $jobid exception
'
test_expect_success 'hello: restore job-exec and run a long testexec job' '
	flux kvs put test.schedutil.mode=success &&
	flux kvs put test.schedutil.R="$(flux R encode -r0-1 -c0-1)" &&
	flux module load job-exec &&
	jobid=$(flux submit -N2 \
		--setattr=system.exec.test.run_duration=100s hostname) &&
	flux job wait-event -t 30 $jobid start
'
test_expect_success 'hello: scheduler reload replays the running job' '
	flux module reload ${SHIM} &&
	test $(flux jobs -no {state} $jobid) = "RUN"
'
test_expect_success 'hello: shim received the full R for the running job' '
	kid=$(flux job id $jobid) &&
	test_wait_until "flux kvs get test.schedutil.hello.$kid" &&
	flux kvs get test.schedutil.hello.$kid >hello_R.json &&
	test_debug "jq -S . <hello_R.json" &&
	jq -e ".execution.R_lite[0].rank == \"0-1\"" <hello_R.json
'
test_expect_success 'hello: clean up the running job' '
	flux cancel $jobid &&
	flux job wait-event -t 30 $jobid clean
'
#
# Exercise the partial-R hello path: when a job hands some of its ranks to
# housekeeping and the scheduler restarts, the job-manager reports the still-
# held ranks via the "free" key and libschedutil hands the shim a partial R
# with the released ranks removed.
#
test_expect_success 'partial: configure housekeeping with a straggler' '
	cat >housekeeping.sh <<-EOT &&
	#!/bin/sh
	test \$(flux getattr rank) -eq 1 && sleep 300
	exit 0
	EOT
	chmod +x housekeeping.sh &&
	flux config load <<-EOT
	[job-manager.housekeeping]
	command = [ "$(pwd)/housekeeping.sh" ]
	release-after = "1s"
	EOT
'
test_expect_success 'partial: run a job and let rank 0 release to housekeeping' '
	jobid=$(flux submit -N2 \
		--setattr=system.exec.test.run_duration=0.1s hostname) &&
	flux job wait-event -t 30 $jobid clean &&
	test_wait_until "one_rank_released $jobid"
'
test_expect_success 'partial: reload scheduler while rank 1 straggles' '
	flux module reload ${SHIM}
'
test_expect_success 'partial: shim received R with the freed rank removed' '
	kid=$(flux job id $jobid) &&
	test_wait_until "flux kvs get test.schedutil.hello.$kid" &&
	flux kvs get test.schedutil.hello.$kid >partial_R.json &&
	test_debug "jq -S . <partial_R.json" &&
	jq -e ".execution.R_lite[0].rank == \"1\"" <partial_R.json
'
test_expect_success 'partial: release the straggler' '
	flux housekeeping kill --all --signal=9 &&
	flux job wait-event -t 30 $jobid clean
'
test_expect_success 'remove housekeeping config' '
	echo {} | flux config load
'
#
# When the scheduler cannot reallocate a running job during the hello
# handshake (ops->hello returns -1), libschedutil raises a fatal exception
# on the job.  Drive that path with the shim hello_fail hook.
#
test_expect_success 'exception: run a long testexec job' '
	flux kvs put test.schedutil.mode=success &&
	flux kvs put test.schedutil.R="$(flux R encode -r0-1 -c0-1)" &&
	jobid=$(flux submit -N2 \
		--setattr=system.exec.test.run_duration=100s hostname) &&
	flux job wait-event -t 30 $jobid start
'
test_expect_success 'exception: hello failure raises a fatal job exception' '
	flux kvs put test.schedutil.hello_fail=1 &&
	flux module reload ${SHIM} &&
	flux job wait-event -t 30 $jobid exception >exception.out &&
	test_debug "cat exception.out" &&
	grep "type=\"scheduler-restart\"" exception.out &&
	flux job wait-event -t 30 $jobid clean &&
	flux kvs put test.schedutil.hello_fail=0
'
test_expect_success 'unload schedutil-shim' '
	flux module remove schedutil-shim
'
#
# schedutil_ready() accepts "unlimited" (tested above via the default) and
# "limited=N"; exercise the latter by loading the shim with ready-mode.
#
test_expect_success 'ready: shim loads with limited concurrency mode' '
	flux module load ${SHIM} ready-mode=limited=2 &&
	jobid=$(flux submit hostname) &&
	flux job wait-event -t 30 $jobid alloc &&
	flux module remove schedutil-shim
'
test_expect_success 'ready: invalid mode is rejected' '
	test_must_fail flux module load ${SHIM} ready-mode=bogus
'
test_expect_success 'restore sched-simple' '
	flux module load sched-simple
'

test_done
