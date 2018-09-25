#!/bin/sh

test_description='Test kvs eventlog get|append'

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

test_expect_success 'flux kvs eventlog append --timestamp works' '
	flux kvs eventlog append --timestamp=1 test.a name some context &&
	flux kvs eventlog get --unformatted test.a >get_a.out &&
	echo "1.000000 name some context" >get_a.exp &&
	test_cmp get_a.exp get_a.out
'

test_expect_success 'flux kvs eventlog append works' '
	flux kvs eventlog append test.b foo &&
	flux kvs eventlog get test.b >get_b.out &&
	grep -q foo get_b.out
'

test_expect_success 'flux kvs eventlog get --watch --count=N works' '
	flux kvs eventlog append --timestamp=42 test.c foo &&
	flux kvs eventlog append --timestamp=43 test.c bar &&
	run_timeout 2 flux kvs eventlog \
		get --watch --unformatted --count=2 test.c >get_c.out &&
	echo "42.000000 foo" >get_c.exp &&
	echo "43.000000 bar" >>get_c.exp &&
	test_cmp get_c.exp get_c.out
'

test_expect_success NO_CHAIN_LINT 'flux kvs eventlog get --watch returns append order' '
	flux kvs eventlog append --timestamp=1 test.d foo bar &&
	echo "1.000000 foo bar" >get_d.exp
	flux kvs eventlog get --unformatted --watch --count=20 test.d >get_d.out &
	pid=$! &&
	for i in $(seq 2 20); do \
		flux kvs eventlog append --timestamp=$i test.d foo bar; \
		echo "$i.000000 foo bar" >>get_d.exp; \
	done &&
	wait $pid &&
	test_cmp get_d.exp get_d.out
'
test_done
