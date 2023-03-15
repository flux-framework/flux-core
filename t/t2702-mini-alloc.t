#!/bin/sh

test_description='flux-mini alloc specific tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
# Set local URI resolution for use of flux-proxy below:
export FLUX_URI_RESOLVE_LOCAL=t
test_under_flux 4 job

flux setattr log-stderr-level 1

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py -f asciicast"

test_expect_success 'flux-mini alloc with no args return error' '
	test_expect_code 1 flux mini alloc
'
test_expect_success 'flux-mini alloc sets command to flux broker' '
	flux mini alloc -n1 --dry-run | \
	    jq -e ".tasks[0].command == [ \"flux\", \"broker\" ]"
'
test_expect_success 'flux-mini alloc appends broker options' '
	flux mini alloc -n1 --broker-opts=-v --dry-run | \
	    jq -e ".tasks[0].command == [ \"flux\", \"broker\", \"-v\" ]"
'
test_expect_success 'flux-mini alloc can set initial-program' '
	flux mini alloc -n1 --dry-run myapp --foo | \
	    jq -e ".tasks[0].command == [ \"flux\", \"broker\", \"myapp\", \"--foo\" ]"
'
test_expect_success 'flux-mini alloc -N2 requests 2 nodes exclusively' '
	flux mini alloc -N2 --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success 'flux-mini alloc --exclusive works' '
	flux mini alloc -N1 -n1 --exclusive --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success 'flux-mini alloc fails if N > n' '
	test_expect_code 1 flux mini alloc -N2 -n1 --dry-run hostname
'
test_expect_success 'flux-mini alloc works' '
	$runpty -o single.out flux mini alloc -n1 \
		flux resource list -s up -no {rlist} &&
	grep "rank0/core0" single.out
'
test_expect_success 'flux-mini alloc works without tty' '
	flux mini alloc -n1 \
		flux resource list -s up -no {rlist} </dev/null >notty.out &&
	test_debug "echo notty: $(cat notty.out)" &&
	test "$(cat notty.out)" = "rank0/core0"
'
test_expect_success 'flux-mini alloc runs one broker per node by default' '
	$runpty -o multi.out flux mini alloc -n5 flux lsattr -v &&
	test_debug "cat multi.out" &&
	grep "size  *2" multi.out
'
test_expect_success 'flux-mini alloc -v prints jobid on stderr' '
	$runpty -o verbose.out flux mini alloc -n1 -v flux lsattr -v &&
	test_debug "cat verbose.out" &&
	grep "jobid: " verbose.out
'
test_expect_success 'flux-mini alloc --bg option works' '
	jobid=$(flux mini alloc -n1 -v --bg) &&
	flux proxy $jobid flux mini run hostname &&
	flux proxy $jobid flux getattr broker.rc2_none &&
	flux shutdown $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success 'flux-mini alloc --bg option works with a command' '
	jobid=$(flux mini alloc -n1 -v --bg /bin/true) &&
	flux job wait-event -t15 -v $jobid finish &&
	flux job attach $jobid
'
test_expect_success 'flux-mini alloc --bg fails if broker fails' '
	test_must_fail flux mini alloc -n1 -v --broker-opts=--xx --bg \
		>badopts.log 2>&1 &&
	test_debug "cat badopts.log" &&
	grep "unrecognized option" badopts.log
'
test_expect_success 'flux-mini alloc --bg fails if rc1 fails' '
	mkdir -p rc1.d/ &&
	cat <<-EOF >rc1.d/rc1-fail &&
	exit 1
	EOF
	( export FLUX_RC_EXTRA=$(pwd) &&
	  test_must_fail flux mini alloc -n1 -v --broker-opts= --bg \
		>rc1-fail.log 2>&1
	) &&
	test_debug "cat rc1-fail.log" &&
	grep "instance startup failed" rc1-fail.log
'


#  Running a process in the background under test_expect_success()
#   causes a copy of the shell to be run in between flux-mini, so the
#   signal can't be delivered to the right PID. Running from a function
#   seems to fix that.
run_mini_bg() {
	flux mini alloc --bg -n1 -v >sigint.log 2>&1 &
	echo $! >sigint.pid
}
waitfile=$SHARNESS_TEST_SRCDIR/scripts/waitfile.lua
test_expect_success NO_CHAIN_LINT 'flux-mini alloc --bg can be interrupted' '
	flux queue stop &&
	test_when_finished "flux queue start" &&
	run_mini_bg &&
	$waitfile -t 20 -v -p waiting sigint.log &&
	kill -INT $(cat sigint.pid) &&
	$waitfile -t 20 -v -p Interrupt sigint.log &&
	wait $pid
'
test_expect_success NO_CHAIN_LINT 'flux-mini alloc --bg errors when job is canceled' '
	flux queue stop &&
	test_when_finished "flux queue start" &&
	flux mini alloc --bg -n1 -v >canceled.log 2>&1 &
	pid=$! &&
	$waitfile -t 20 -v -p waiting canceled.log &&
	flux cancel --all &&
	cat canceled.log &&
	test_must_fail wait $pid &&
	grep "unexpectedly exited" canceled.log
'
test_expect_success 'flux mini alloc: sets mpi=none by default' '
	flux mini alloc -N1 --dry-run hostname | \
		jq -e ".attributes.system.shell.options.mpi = \"none\""
'
test_expect_success 'flux mini alloc: mpi option can be overridden' '
	flux mini alloc -o mpi=foo -N1 --dry-run hostname | \
		jq -e ".attributes.system.shell.options.mpi = \"foo\""
'
test_expect_success 'flux mini alloc: MPI vars are not set in initial program' '
	flux queue start &&
	unset OMPI_MCA_pmix &&
	flux mini alloc -N1 printenv >envtest.out &&
	test_must_fail grep OMPI_MCA_pmix envtest.out
'
test_expect_success 'flux mini alloc: --dump works' '
        jobid=$(flux mini alloc -N1 --bg --dump) &&
	flux shutdown $jobid &&
	flux job wait-event $jobid clean &&
        tar tvf flux-${jobid}-dump.tgz
'
test_expect_success 'flux mini alloc: --dump=FILE works' '
        jobid=$(flux mini alloc -N1 --bg --dump=testdump.tgz) &&
	flux shutdown $jobid &&
	flux job wait-event $jobid clean &&
        tar tvf testdump.tgz
'
test_expect_success 'flux mini alloc: --dump=FILE works with mustache' '
        jobid=$(flux mini alloc -N1 --bg --dump=testdump-{{id}}.tgz) &&
	flux shutdown $jobid &&
	flux job wait-event $jobid clean &&
        tar tvf testdump-${jobid}.tgz
'


test_done
