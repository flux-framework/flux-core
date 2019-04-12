#!/bin/sh

test_description='Test kvs eventlog get|append'

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

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

test_expect_success 'flux kvs eventlog append fails with invalid context' '
	! flux kvs eventlog append test.c foo not_a_object
'

test_done
