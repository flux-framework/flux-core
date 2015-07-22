#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
test_under_flux 4

test_expect_success 'wreckrun: works' '
	hostname=$(hostname) &&
	run_timeout 2 flux wreckrun -n4 -N4 hostname  >output &&
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
	cd testdir && 
	mypwd=$(pwd) &&
	run_timeout 5 flux wreckrun -N1 -n1 pwd | grep "^$mypwd$"
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
test_done
