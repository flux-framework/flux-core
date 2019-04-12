#!/bin/sh

test_description='Test kvs eventlog get|append'

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

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

test_done
