#!/bin/sh
#

test_description='Test exec command --jobid option

Test exec --jobid functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} job

if flux job submit --help 2>&1 | grep -q sign-type; then
        test_set_prereq FLUX_SECURITY
fi

TMPDIR=$(cd /tmp && $(which pwd))

test_expect_success 'flux exec --jobid fails with invalid job id' '
	test_expect_code 1 flux exec --jobid=f-o-o true 2>badid.err &&
	test_debug "cat badid.err" &&
	grep "error parsing jobid" badid.err
'
test_expect_success 'flux exec --jobid fails for nonexistent job id' '
	test_expect_code 1 flux exec --jobid=f1 true 2>noid.err &&
	test_debug "cat noid.err" &&
	grep "not found" noid.err
'
test_expect_success 'run two jobs on different ranks' '
	flux submit --wait-event=start --bcc=1-2 -N1 sleep inf &&
	id1=$(flux job last [-1:]) &&
	id2=$(flux job last) &&
	test_debug "flux jobs -no \"{id} {ranks}\""
'
test_expect_success 'flux exec --jobid works' '
	rank1=$(flux exec --jobid=$id1 flux getattr rank) &&
	rank2=$(flux exec --jobid=$id2 flux getattr rank) &&
	test_debug "echo flux exec --jobid=$id1 ran on rank $rank1" &&
	test_debug "echo flux exec --jobid=$id2 ran on rank $rank2" &&
	test $rank1 -eq $(flux jobs -no {ranks} $id1) &&
	test $rank2 -eq $(flux jobs -no {ranks} $id2)
'
test_expect_success 'run one job on two ranks' '
	jobid=$(flux submit --wait-event=start -N2 sleep inf)
'
test_expect_success 'flux exec --jobid on multi-node jobs runs on all ranks' '
	flux exec --jobid=$jobid --label-io flux getattr rank >2node.out &&
	test_debug "cat 2node.out" &&
	grep "2: 2" 2node.out &&
	grep "3: 3" 2node.out
'
test_expect_success 'flux exec --jobid works with --rank option' '
	flux exec --jobid=$jobid -r 0 flux getattr rank &&
	test $(flux exec --jobid=$jobid -r 0 flux getattr rank) -eq 2 &&
	flux exec --jobid=$jobid -r 1 flux getattr rank &&
	test $(flux exec --jobid=$jobid -r 1 flux getattr rank) -eq 3
'
test_expect_success 'flux exec --jobid fails with invalid --rank option' '
	test_must_fail flux exec --jobid=$jobid -r 3 hostname &&
	test_must_fail flux exec --jobid=$jobid -r 0-3 hostname
'
test_expect_success 'flux exec --jobid works with --exclude option' '
	flux exec --jobid=$jobid -x 0 flux getattr rank &&
	test $(flux exec --jobid=$jobid -x 0 flux getattr rank) -eq 3 &&
	flux exec --jobid=$jobid -x 1 flux getattr rank &&
	test $(flux exec --jobid=$jobid -x 1 flux getattr rank) -eq 2
'
test_expect_success 'flux exec --jobid ignores invalid --exclude ranks' '
	test_debug "flux exec --jobid=$jobid -l -x 3-4 flux getattr rank" &&
	test $(flux exec --jobid=$jobid -x 1-5 flux getattr rank) -eq 2
'
test_expect_success 'flux exec --jobid fails if there are no ranks to target' '
	test_must_fail flux exec --jobid=$jobid -x 0-1 hostname
'
test_exec() {
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=$1 \
	    flux exec -r$3 --jobid=$2 id
}
test_expect_success FLUX_SECURITY 'flux exec --jobid fails from other user' '
	alt_userid=$(($(id -u)+1)) &&
	test_debug "echo testing with handle userid=$alt_userid" &&
	test_must_fail test_exec $alt_userid $jobid 0 2>eperm0.err &&
	test_debug "cat eperm0.err" &&
	grep "failed to get shell.init event" eperm0.err &&
	test_must_fail test_exec $alt_userid $jobid 1 2>eperm1.err &&
	test_debug "cat eperm1.err" &&
	grep "failed to get shell.init event" eperm1.err
'
job_service() {
	flux job eventlog --format=json -p exec $1 \
	    | jq -r 'select(.name == "shell.init") .context.service'
}
# Usage: test_exec_direct userid rolemask jobid
test_exec_direct() {
	service=$(job_service $2).rexec && \
	ranks=$(flux jobs -n --format="{ranks}" $2) && \
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=$1 \
	    flux exec -r$ranks --service=$service id
}
test_expect_success FLUX_SECURITY 'flux exec direct to shell fails also' '
	alt_userid=$(($(id -u)+1)) &&
	test_must_fail test_exec_direct $alt_userid $jobid 2>eperm.err &&
	test_debug "cat eperm.err" &&
	grep "requires owner credentials" eperm.err
'

test_expect_success 'cancel jobs' '
	flux cancel --all &&
	flux job wait-event $id1 clean
'
test_expect_success 'flux exec --jobid on inactive job fails' '
	test_must_fail flux exec --jobid=$id1 hostname 2>inactive.err &&
	test_debug "cat inactive.err" &&
	grep "not currently running" inactive.err
'

test_done
