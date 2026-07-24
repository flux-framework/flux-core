#!/bin/sh

test_description='Test job-exec reattach across a module reload'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

# A running job's guest namespace is grafted into the primary namespace at
# job.<id>.guest when job-exec unloads, but (unlike a full instance restart)
# the live namespace is retained across a module reload since the KVS
# persists.  On reload, the job manager re-issues the job's start request with
# reattach set, and job-exec recreates the namespace from the graft -- which
# collides with the retained live namespace (EEXIST) and is resolved by
# adopting the live namespace in place.  This exercises the same-KVS reattach
# path that a full restart (t3202) does not.

lastevent() { flux job eventlog $1 | awk 'END{print $2}'; }

test_expect_success 'submit a long-running testexec job and wait for start' '
	id=$(flux submit --flags=debug \
	                 --setattr=system.exec.test.run_duration=100s \
	                 hostname) &&
	flux job wait-event -t 60 ${id} start
'
test_expect_success 'write a canary into the job guest namespace' '
	ns=$(flux job namespace ${id}) &&
	flux kvs put -N ${ns} warmstart.canary=hello-3204
'
test_expect_success 'reload job-exec module' '
	flux module reload job-exec
'
test_expect_success 'job is reattached rather than relaunched' '
	flux job wait-event -t 60 ${id} debug.exec-reattach-finish &&
	flux job eventlog ${id} >eventlog.out &&
	test_debug "cat eventlog.out" &&
	grep "debug.start-lost" eventlog.out &&
	grep "debug.exec-reattach-finish" eventlog.out
'
test_expect_success 'job remains in RUN state after reload' '
	test $(flux jobs -no "{state}" ${id}) = RUN
'
test_expect_success 'guest namespace was adopted, canary survives' '
	ns=$(flux job namespace ${id}) &&
	flux kvs get -N ${ns} warmstart.canary >canary.out &&
	grep hello-3204 canary.out
'
test_expect_success 'cancel job and wait for it to clean up' '
	flux cancel ${id} &&
	flux job wait-event -t 60 ${id} clean
'
test_done
