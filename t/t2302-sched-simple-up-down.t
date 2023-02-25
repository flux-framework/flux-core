#!/bin/sh

test_description='sched-simple up/down/drain tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 job

query="flux resource list --state=free -no {rlist}"

flux R encode -r0-1 -c0-3 >R.test

check_resource()
{
	local name="$1"
	local state="$2"
	local expected="$3"
	local result="$(flux resource list --state=$state -no {$name})"
	echo "# check $name in $state is $expected (got $result)"
	test "$result" = "$expected"
}
check_ncores() { check_resource "ncores" "$@"; }
check_nnodes() { check_resource "nnodes" "$@"; }
check_rlist() { check_resource "rlist" "$@"; }

test_expect_success 'unload job-exec module to prevent job execution' '
	flux module remove job-exec
'
test_expect_success 'sched-simple: reload ingest module without validator' '
	flux module reload job-ingest disable-validator  &&
	flux exec -r all -x 0 flux module reload job-ingest disable-validator
'

test_expect_success 'sched-simple: reload sched-simple with default resource.R' '
	flux module unload sched-simple &&
	flux resource reload R.test &&
	flux module load sched-simple &&
	flux dmesg 2>&1 >reload.dmesg.log &&
	test_debug "grep sched-simple reload.dmesg.log" &&
	grep "ready:.*rank\[0-1\]/core\[0-3\]" reload.dmesg.log &&
	test_debug "echo result=\"$($query)\"" &&
	test "$($query)" = "rank[0-1]/core[0-3]"
'
test_expect_success 'sched-simple: all nodes up after reload' '
	test_debug "flux resource list --format=rlist --states=up,down,allocated" &&
	check_rlist "free" "rank[0-1]/core[0-3]" &&
	check_nnodes "up" 2 &&
	check_ncores "up" 8 &&
	check_ncores "down" 0
'
test_expect_success 'sched-simple: flux-resource drain works' '
	flux resource drain 0 &&
	check_rlist "up" "rank1/core[0-3]" &&
	check_nnodes "up" 1 &&
	check_ncores "up" 4 &&
	check_rlist "down" "rank0/core[0-3]"
'
test_expect_success 'sched-simple: job not allocated drained resources' '
	id=$(flux submit -n1 hostname) &&
	flux job wait-event $id alloc &&
	check_rlist "allocated" "rank1/core0" &&
	check_rlist "free" "rank1/core[1-3]" &&
	check_ncores "allocated" 1 &&
	check_ncores "free" 3
'
test_expect_success 'sched-simple: drain allocated resources works' '
	flux resource drain 1 &&
	check_rlist "down" "rank[0-1]/core[0-3]" &&
	check_ncores "up" 0
'
test_expect_success 'sched-simple: down+alloc resources listed as allocated' '
	check_rlist "allocated" "rank1/core0"
'
test_expect_success 'sched-simple: submitted job blocks with no up resources' '
	id=$(flux submit -n1 hostname) &&
	test_expect_code 1 flux job wait-event -t 0.25 $id alloc &&
	check_ncores "allocated" 1
'
test_expect_success 'sched-simple: job is scheduled when resource undrained' '
	flux resource undrain 0 &&
	flux job wait-event -t5 $id alloc &&
	check_rlist "allocated" "rank[0-1]/core0" &&
	check_nnodes "allocated" 2 &&
	check_ncores "allocated" 2
'
test_done
