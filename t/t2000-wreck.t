#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE}

test_expect_success 'wreckrun: works' '
	hostname=$(hostname) &&
	run_timeout 5 flux wreckrun -n${SIZE} hostname  >output &&
	for i in $(seq 1 ${SIZE}); do echo $hostname; done >expected &&
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
test_expect_success 'wreckrun: does not propagate FLUX_URI' '
	run_timeout 5 flux wreckrun -n${SIZE} printenv FLUX_URI >uri_output &&
	test `sort uri_output | uniq | wc -l` -eq ${SIZE}
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
	   flux wreckrun -n${SIZE} /bin/true > output &&
        tail -4 output > output_states && # only care about last 4
	cat >expected_states <<-EOF &&
	wreck.state.reserved
	wreck.state.starting
	wreck.state.running
	wreck.state.complete
	EOF
	test_cmp expected_states output_states
'
test_expect_success 'wreck: signaling wreckrun works' '
        flux wreckrun -n${SIZE} sleep 15 </dev/null &
	q=$! &&
	$SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
           wreck.state wreck.state.running /bin/true &&
        sleep 0.5 &&
	kill -INT $q &&
	test_expect_code 137 wait $q
'
flux kvs dir -r resource >/dev/null 2>&1 && test_set_prereq RES_HWLOC
test_expect_success RES_HWLOC 'wreckrun: oversubscription of tasks' '
	run_timeout 15 flux wreckrun -v -n$(($(nproc)*${SIZE}+1)) /bin/true
'
test_expect_success 'wreckrun: uneven distribution with -n, -N' '
	run_timeout 10 flux wreckrun -N${SIZE} -n$((${SIZE}+1)) /bin/true
'
test_expect_success 'wreckrun: too many nodes requested fails' '
	test_expect_code 1 run_timeout 10 flux wreckrun -N$((${SIZE}+1)) hostname
'
test_done
