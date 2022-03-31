#!/bin/sh

test_description='flux-mini alloc specific tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
test_under_flux 4 job

flux setattr log-stderr-level 1

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py -f asciicast"

test_expect_success 'flux-mini alloc with no args return error' '
	test_expect_code 1 flux mini alloc
'
test_expect_success HAVE_JQ 'flux-mini alloc sets command to flux broker' '
	flux mini alloc -n1 --dry-run | \
	    jq -e ".tasks[0].command == [ \"flux\", \"broker\" ]"
'
test_expect_success HAVE_JQ 'flux-mini alloc appends broker options' '
	flux mini alloc -n1 --broker-opts=-v --dry-run | \
	    jq -e ".tasks[0].command == [ \"flux\", \"broker\", \"-v\" ]"
'
test_expect_success HAVE_JQ 'flux-mini alloc can set initial-program' '
	flux mini alloc -n1 --dry-run myapp --foo | \
	    jq -e ".tasks[0].command == [ \"flux\", \"broker\", \"myapp\", \"--foo\" ]"
'
test_expect_success HAVE_JQ 'flux-mini alloc -N2 requests 2 nodes exclusively' '
	flux mini alloc -N2 --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success HAVE_JQ 'flux-mini alloc --exclusive works' '
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
test_done
