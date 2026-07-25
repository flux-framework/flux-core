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
# The recoverable event (RFC 50) gates reattach: without it, generic job-exec
# code raises a fatal exception rather than relaunching the job's shells.
# Strip the event from a running job's exec.eventlog, then reload to confirm
# the gate fires.
test_expect_success 'submit another job and wait for recoverable event' '
	id2=$(flux submit --flags=debug \
	                  --setattr=system.exec.test.run_duration=100s \
	                  hostname) &&
	ns2=$(flux job namespace ${id2}) &&
	flux job wait-event -t 60 ${id2} start &&
	run_timeout 60 flux kvs eventlog wait-event -N ${ns2} \
	    exec.eventlog recoverable
'
test_expect_success 'strip recoverable event from exec.eventlog' '
	flux kvs eventlog get -u -N ${ns2} exec.eventlog \
	    | jq -c "select(.name != \"recoverable\")" >stripped.out &&
	flux kvs put --raw -N ${ns2} exec.eventlog=- <stripped.out
'
test_expect_success 'reload job-exec module' '
	flux module reload job-exec
'
test_expect_success 'non-recoverable job raises exec exception on reattach' '
	flux job wait-event -t 60 ${id2} exception &&
	flux job eventlog ${id2} >eventlog2.out &&
	test_debug "cat eventlog2.out" &&
	grep -q "type=\"exec\"" eventlog2.out &&
	grep -q "job is not recoverable" eventlog2.out
'
test_expect_success 'clean up non-recoverable job' '
	flux job wait-event -t 60 ${id2} clean
'
# The real (bulk-exec) executor does not implement reattach, so the generic
# gate raises a fatal exception rather than relaunching the shells -- the same
# terminal behavior as a full restart (t3202), but reached via the module
# reload / in-place namespace adoption path.  --input=/dev/null avoids the
# shell's KVS stdin watcher, which is not torn down by a bare module reload
# but is left in flight otherwise; dropping it keeps the reattach reject the
# only thing that can fail the job.
test_expect_success 'submit a real (bulk-exec) job and wait for start' '
	id3=$(flux submit --flags=debug --input=/dev/null \
	                  --wait-event=start sleep 300)
'
test_expect_success 'reload job-exec module' '
	flux module reload job-exec
'
test_expect_success 'bulk-exec job raises exec exception on reattach' '
	flux job wait-event -t 60 ${id3} exception &&
	flux job eventlog ${id3} >eventlog3.out &&
	test_debug "cat eventlog3.out" &&
	grep -q "type=\"exec\"" eventlog3.out &&
	grep -q "reattach to running job is not implemented" eventlog3.out
'
test_expect_success 'clean up bulk-exec job' '
	flux job wait-event -t 60 ${id3} clean
'
test_done
