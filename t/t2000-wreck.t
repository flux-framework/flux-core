#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
test_under_flux 4

test_expect_success 'wreckrun: works' '
	hostname=$(hostname) &&
	run_timeout 5 flux wreckrun -n4 -N4 hostname  >output &&
	cat >expected <<-EOF  &&
	$hostname
	$hostname
	$hostname
	$hostname
	EOF
	test_cmp expected output
'

test_expect_success 'wreckrun: propagates current working directory' '
	mkdir -p testdir &&
	mypwd=$(pwd)/testdir &&
	( cd testdir &&
	run_timeout 5 flux wreckrun -N1 -n1 pwd ) | grep "^$mypwd$"
'
test_expect_success 'wreckrun: propagates current environment' '
	( export MY_UNLIKELY_ENV=0xdeadbeef &&
	  run_timeout 5 flux wreckrun -N1 -n1 env ) | \
           grep "MY_UNLIKELY_ENV=0xdeadbeef"
'
test_expect_success 'wreckrun: does not drop output' '
	for i in `seq 0 100`; do 
		base64 /dev/urandom | head -c77
	done >expected &&
	run_timeout 5 flux wreckrun -N1 -n1 cat expected >output &&
	test_cmp expected output
'
test_expect_success 'wreck: job state events emitted' '
	run_timeout 5 \
	  $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
	   wreck.state wreck.state.complete \
	   flux wreckrun -N4 -n4 /bin/true > output &&
	cat >expected <<-EOF &&
	wreck.state.starting
	wreck.state.running
	wreck.state.complete
	EOF
	test_cmp expected output
'
test_expect_success 'wreck: signaling wreckrun works' '
        run_timeout 5 flux wreckrun -N4 -n4 sleep 100 </dev/null &
	q=$! &&
	sleep 1 &&
        echo killing $q &&
	kill $q &&
	test_expect_code 143 wait $q
'
test_done
