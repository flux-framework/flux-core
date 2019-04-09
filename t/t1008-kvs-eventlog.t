#!/bin/sh

test_description='Test kvs eventlog get|append'

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

test_expect_success 'flux kvs eventlog append --timestamp works' '
	flux kvs eventlog append --timestamp=1 test.a name {\"data\":\"foo\"} &&
	flux kvs eventlog get --unformatted test.a >get_a.out &&
        echo "{\"timestamp\":1.0,\"name\":\"name\",\"context\":{\"data\":\"foo\"}}" \
             >get_a.exp &&
        flux kvs get test.a &&
        test_cmp get_a.exp get_a.out
'

test_expect_success 'flux kvs eventlog append works w/o context' '
	flux kvs eventlog append test.b foo &&
	flux kvs eventlog get test.b >get_b.out &&
	grep foo get_b.out
'

test_expect_success 'flux kvs eventlog append works w/ context' '
	flux kvs eventlog append test.c foo {\"data\":\"bar\"} &&
	flux kvs eventlog get test.c >get_c.out &&
	grep -q foo get_c.out &&
        grep -q "\"data\":\"bar\"" get_c.out
'

test_expect_success 'flux kvs eventlog get --watch --count=N works' '
	flux kvs eventlog append --timestamp=42 test.d foo &&
	flux kvs eventlog append --timestamp=43 test.d bar &&
	run_timeout 2 flux kvs eventlog \
		get --watch --unformatted --count=2 test.d >get_d.out &&
	echo "{\"timestamp\":42.0,\"name\":\"foo\"}" >get_d.exp &&
	echo "{\"timestamp\":43.0,\"name\":\"bar\"}" >>get_d.exp &&
        test_cmp get_d.exp get_d.out
'

test_expect_success NO_CHAIN_LINT 'flux kvs eventlog get --watch returns append order' '
	flux kvs eventlog append --timestamp=1 test.e foo "{\"data\":\"bar\"}" &&
	echo "{\"timestamp\":1.0,\"name\":\"foo\",\"context\":{\"data\":\"bar\"}}" >get_e.exp
	flux kvs eventlog get --unformatted --watch --count=20 test.e >get_e.out &
	pid=$! &&
	for i in $(seq 2 20); do \
		flux kvs eventlog append --timestamp=$i test.e foo "{\"data\":\"bar\"}"; \
    	        echo "{\"timestamp\":$i.0,\"name\":\"foo\",\"context\":{\"data\":\"bar\"}}" >>get_e.exp
	done &&
	wait $pid &&
	test_cmp get_e.exp get_e.out
'

test_expect_success 'flux kvs eventlog append fails with invalid context' '
	! flux kvs eventlog append test.c foo not_a_object
'

test_done
