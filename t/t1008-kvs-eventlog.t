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

test_expect_success 'flux kvs eventlog append works w/o context' '
	flux kvs eventlog append test.b foo &&
	flux kvs eventlog get test.b >get_b.out &&
	grep -q foo get_b.out
'

test_expect_success 'flux kvs eventlog append works w/ context' '
	flux kvs eventlog append test.c foo bar &&
	flux kvs eventlog get test.c >get_c.out &&
	grep -q foo get_c.out &&
        grep -q bar get_c.out
'

test_expect_success 'flux kvs eventlog get --watch --count=N works' '
	flux kvs eventlog append --timestamp=42 test.d foo &&
	flux kvs eventlog append --timestamp=43 test.d bar &&
	run_timeout 2 flux kvs eventlog \
		get --watch --unformatted --count=2 test.d >get_d.out &&
	echo "42.000000 foo" >get_d.exp &&
	echo "43.000000 bar" >>get_d.exp &&
	test_cmp get_d.exp get_d.out
'

test_expect_success NO_CHAIN_LINT 'flux kvs eventlog get --watch returns append order' '
	flux kvs eventlog append --timestamp=1 test.e foo bar &&
	echo "1.000000 foo bar" >get_e.exp
	flux kvs eventlog get --unformatted --watch --count=20 test.e >get_e.out &
	pid=$! &&
	for i in $(seq 2 20); do \
		flux kvs eventlog append --timestamp=$i test.e foo bar; \
		echo "$i.000000 foo bar" >>get_e.exp; \
	done &&
	wait $pid &&
	test_cmp get_e.exp get_e.out
'
test_done
