#!/bin/sh
test_description='Test update of job queue'

. $(dirname $0)/sharness.sh

if flux job submit --help 2>&1 | grep -q sign-type; then
	test_set_prereq HAVE_FLUX_SECURITY
fi

test_under_flux 4 full

flux setattr log-stderr-level 1

test_expect_success 'config queues and resources' '
	flux R encode -r 0-3 -p batch:0-2 -p debug:3 \
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	policy.limits.duration = "1h"
	policy.jobspec.defaults.system.duration = "1h"

	[queues.debug]
	requires = [ "debug" ]
	policy.limits.duration = "1m"
	policy.jobspec.defaults.system.duration = "1m"

	[queues.other]

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
	flux queue start --all &&
	flux module unload sched-simple &&
	flux module reload resource &&
	flux module load sched-simple &&
	flux queue list &&
	flux resource list -o rlist
'
test_expect_success 'invalid queue update RPC fails' '
	jobid=$(flux submit --urgency=hold -n1 hostname) &&
	echo "{\"id\": $(flux job id $jobid),\
	       \"updates\": {\"attributes.system.queue\": 42}\
	      }" \
            | ${FLUX_BUILD_DIR}/t/request/rpc job-manager.update 22 && # EINVAL
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success 'update of invalid job fails' '
	test_must_fail flux update f1234 queue=batch
'
test_expect_success 'update queue of running job fails' '
	jobid=$(flux submit --wait-event=start -n1 sleep 300) &&
	test_must_fail flux update $jobid queue=debug 2>running.err &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean &&
	grep "update of queue for running job not supported" running.err
'
test_expect_success 'update to invalid queue fails' '
	jobid=$(flux submit -q debug --urgency=hold hostname) &&
	test_must_fail flux update $jobid queue=foo
'
test_expect_success 'update to same queue fails' '
	test_must_fail flux update $jobid queue=debug
'
test_expect_success 'update to batch queue works' '
	flux update $jobid queue=batch &&
	test_debug "flux job eventlog $jobid" &&
	flux job eventlog $jobid \
	  | grep jobspec-update \
	  | grep attributes.system.queue=\"batch\"
'
test_expect_success 'job runs on batch resources' '
	flux job urgency $jobid default &&
	flux job wait-event $jobid clean &&
	test "$(flux jobs -no {queue} $jobid)" = "batch" &&
	test $(flux jobs -no {ranks} $jobid) = "0"
'
test_expect_success 'update to queue with lower duration limit fails' '
	jobid=$(flux submit -q batch --urgency=hold hostname) &&
	test_must_fail flux update $jobid queue=debug 2>queue.err &&
	grep "duration.*exceeds policy limit" queue.err
'
test_expect_success 'update of duration allows queue update' '
	flux update $jobid queue=debug duration=1m  &&
	test_debug "flux job eventlog $jobid" &&
	flux job eventlog $jobid \
	  | grep jobspec-update \
	  | grep attributes.system.queue=\"debug\"
'
test_expect_success 'job runs on debug resources' '
	flux job urgency $jobid default &&
	flux job wait-event $jobid clean &&
	test "$(flux jobs -no {queue} $jobid)" = "debug" &&
	test $(flux jobs -no {ranks} $jobid) = "3"
'
test_expect_success 'update of infeasible job to queue fails' '
	jobid=$(flux submit -q batch -N2 --urgency=hold hostname) &&
	test_debug "flux jobs -a" &&
	test_must_fail flux update $jobid duration=1m queue=debug \
		2>infeasible.err &&
	test_debug "cat infeasible.err" &&
	flux cancel $jobid
'
test_expect_success 'update of queue for job with other constraints fails' '
	jobid=$(flux submit --urgency=hold \
		-q batch -N1 -t 1m --requires=rank:0 hostname) &&
	test_must_fail flux update $jobid queue=debug \
		2>constraints.err &&
	test_debug "cat constraints.err" &&
	grep "unable to update queue" constraints.err &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success 'update from queue with no constraints works' '
	jobid=$(flux submit -t 1m --urgency=hold -q other hostname) &&
	flux update $jobid queue=debug &&
	test_debug "flux job eventlog $jobid" &&
	flux job eventlog $jobid \
	  | grep jobspec-update \
	  | grep attributes.system.queue=\"debug\" &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success 'update to queue with no constraints works' '
	jobid=$(flux submit --urgency=hold -q debug hostname) &&
	flux update $jobid queue=other &&
	test_debug "flux job eventlog $jobid" &&
	flux job eventlog $jobid \
	  | grep jobspec-update \
	  | grep attributes.system.queue=\"other\"
'
test_expect_success 'job can still run' '
	flux job urgency $jobid default &&
	flux job attach $jobid
'
test_expect_success "job with constraints in a queue without can't be updated" '
	jobid=$(flux submit -t 1m --urgency=hold -q other \
		--requires=rank:0 hostname) &&
	test_must_fail flux update $jobid queue=debug &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success 'update-queue plugin can be unloaded' '
	flux jobtap remove .update-queue &&
	flux jobtap list -a >jobtap-list.out &&
	test_must_fail grep update-queue jobtap-list.out
'
test_expect_success 'updates are now disabled' '
	jobid=$(flux submit -q debug --urgency=hold hostname) &&
	test_must_fail flux update $jobid queue=batch
'
test_expect_success 'update-queue plugin can be reloaded' '
	flux jobtap load .update-queue &&
	flux jobtap list -a >jobtap-list2.out &&
	grep update-queue jobtap-list2.out
'
test_expect_success 'updates are reenabled' '
	flux update $jobid queue=batch &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean &&
	flux job eventlog $jobid \
	  | grep jobspec-update \
	  | grep attributes.system.queue=\"batch\"
'
test_done
