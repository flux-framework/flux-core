#!/bin/sh

test_description='Test broker log ring buffer' 

. `dirname $0`/sharness.sh

test_under_flux 4

test_expect_success 'flux getattr log-count counts log messages' '
	OLD_VAL=`flux getattr log-count` &&
	flux logger --priority test.debug hello &&
	NEW_VAL=`flux getattr log-count` &&
	test "${OLD_VAL}" -lt "${NEW_VAL}"
'
test_expect_success 'flux getattr log-ring-used counts log messages' '
	OLD_VAL=`flux getattr log-ring-used` &&
	flux logger --priority test.debug hello &&
	NEW_VAL=`flux getattr log-ring-used` &&
	test "${OLD_VAL}" -lt "${NEW_VAL}"
'
test_expect_success 'flux dmesg -C clears, no print' '
	flux logger --priority test.debug hello &&
	OLD_VAL=`flux getattr log-ring-used` &&
	test "${OLD_VAL}" -gt 0 &&
	flux dmesg -C > dmesg.out &&
	! grep -q hello_dmesg dmesg.out &&
	NEW_VAL=`flux getattr log-ring-used` &&
	test "${NEW_VAL}" -eq 0
'
test_expect_success 'flux setattr log-ring-size trims ring buffer' '
	flux logger --priority test.debug hello &&
	flux logger --priority test.debug hello &&
	flux logger --priority test.debug hello &&
	flux logger --priority test.debug hello &&
	OLD_VAL=`flux getattr log-ring-used` &&
	test "${OLD_VAL}" -ge 4 &&
	flux setattr log-ring-size 4 &&
	NEW_VAL=`flux getattr log-ring-used` &&
	test "${NEW_VAL}" -eq 4
'
test_expect_success 'flux dmesg prints, no clear' '
	flux logger --priority test.debug hello_dmesg &&
	flux dmesg >dmesg.out &&
	grep -q hello_dmesg dmesg.out
	flux dmesg >dmesg.out &&
	grep -q hello_dmesg dmesg.out
'
test_expect_success 'flux dmesg -c prints and clears' '
	flux logger --priority test.debug hello_dmesg &&
	flux dmesg -c >dmesg.out &&
	grep -q hello_dmesg dmesg.out &&
	flux dmesg >dmesg.out &&
	! grep -q hello_dmesg dmesg.out
'
test_expect_success 'ring buffer wraps over old entries' '
	flux setattr log-ring-size 2 &&
	flux logger --priority test.debug hello1 &&
	flux logger --priority test.debug hello2 &&
	flux logger --priority test.debug hello3 &&
	ATTR_VAL=`flux getattr log-ring-used` &&
	test "${ATTR_VAL}" -eq 2 &&
	flux dmesg >dmesg.out &&
	! grep -q hello1 dmesg.out &&
	grep -q hello2 dmesg.out &&
	grep -q hello3 dmesg.out
'

test_done
