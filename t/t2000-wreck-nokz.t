#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE} wreck

test_expect_success 'set global nokz option' '
	flux wreck setopt nokz && flux wreck getopt nokz
'
test_expect_success 'wreckrun nokz: works' '
	hostname=$(hostname) &&
	run_timeout 5 flux wreckrun -n${SIZE} hostname  >output &&
	for i in $(seq 1 ${SIZE}); do echo $hostname; done >expected &&
	test_cmp expected output
'
test_debug "flux kvs get lwj.0.0.1.ioservice"
test_expect_success 'wreckrun nokz: no kz streams in kvs' '
	test_expect_code 1 flux kvs dir $(flux wreck last-jobid -p).0.stdout &&
	test_expect_code 1 flux kvs dir $(flux wreck last-jobid -p).0.stderr
'
test_expect_success 'wreckrun nokz: -o nokz=false disables nokz' '
	hostname=$(hostname) &&
	run_timeout 5 flux wreckrun -n${SIZE} -o nokz=false hostname  >output &&
	for i in $(seq 1 ${SIZE}); do echo $hostname; done >expected &&
	test_cmp expected output &&
	flux kvs dir $(flux wreck last-jobid -p).0.stdout &&
	flux kvs dir $(flux wreck last-jobid -p).0.stderr
'
test_expect_success 'wreckrun nokz: -o kz disables nokz' '
	hostname=$(hostname) &&
	run_timeout 5 flux wreckrun -n${SIZE} -o kz hostname >output.kz &&
	for i in $(seq 1 ${SIZE}); do echo $hostname; done   >expected.kz &&
	test_cmp expected.kz output.kz &&
	flux kvs dir $(flux wreck last-jobid -p).0.stdout &&
	flux kvs dir $(flux wreck last-jobid -p).0.stderr
'
test_expect_success 'wreckrun nokz: kz streams in kvs' '
	flux kvs dir $(flux wreck last-jobid -p).0.stdout &&
	flux kvs dir $(flux wreck last-jobid -p).0.stderr &&
	flux wreck attach $(flux wreck last-jobid) > output.kz &&
	test_cmp expected output.kz
'
test_expect_success 'wreckrun nokz: does not drop output' '
	for i in `seq 0 100`; do 
		base64 /dev/urandom | head -c77
	done >expected &&
	run_timeout 5 flux wreckrun -N1 -n1 cat expected >output &&
	test_cmp expected output
'
test_expect_success 'wreckrun nokz: handles stdin' '
	cat >expected.stdin <<-EOF &&
	This is a test.

	EOF
	cat expected.stdin | flux wreckrun -T5s cat > output.stdin &&
        test_cmp expected.stdin output.stdin
'
test_expect_success 'wreckrun nokz: handles empty stdin' '
	flux wreckrun -T5s cat > output.devnull </dev/null &&
        test_must_fail test -s output.devnull
'
WAITFILE="$SHARNESS_TEST_SRCDIR/scripts/waitfile.lua"

test_expect_success 'wreckrun nokz: --output supported' '
	flux wreckrun --output=test1.out echo hello &&
        $WAITFILE --timeout=1 --pattern=hello test1.out
'
test_expect_success 'wreckrun nokz: --error supported' '
	flux wreckrun --output=test2.out --error=test2.err \
	    sh -c "echo >&2 this is stderr; echo this is stdout" &&
        $WAITFILE -v --timeout=1 -p "this is stderr" test2.err &&
        $WAITFILE -v --timeout=1 -p "this is stdout" test2.out
'
test_expect_success 'wreckrun nokz: --error without --output supported' '
	flux wreckrun --error=test3.err >test3.out \
	    sh -c "echo >&2 this is stderr; echo this is stdout" &&
        $WAITFILE -v --timeout=1 -p "this is stderr" test3.err &&
        $WAITFILE -v --timeout=1 -p "this is stdout" test3.out
'
test_expect_success 'wreckrun nokz: error and output on different ranks' '
	flux exec -r 1 -- flux wreckrun --error=test4.err >test4.out \
	    sh -c "echo >&2 this is stderr; echo this is stdout" &&
        $WAITFILE -v --timeout=1 -p "this is stderr" test4.err &&
        $WAITFILE -v --timeout=1 -p "this is stdout" test4.out
'

KVSWAIT="$SHARNESS_TEST_SRCDIR/scripts/kvs-watch-until.lua"

test_expect_success 'wreckrun nokz: --output=kvs://key works' '
	flux wreckrun -n 4 --output=kvs://test-output echo hello &&
	$KVSWAIT -t 1 $(flux wreck last-jobid -p).test-output \
		"v  == \"hello\nhello\nhello\nhello\n\""
'
test_expect_success 'wreckrun nokz: --error=kvs://key works' '
	flux wreckrun -n 4 --error=kvs://test-stderr \
			sh -c "echo >&2 an error"  &&
	$KVSWAIT -t 1 $(flux wreck last-jobid -p).test-stderr \
		"v  == \"an error\nan error\nan error\nan error\n\""
'
test_done
