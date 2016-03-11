#!/bin/sh

test_description='Test broker log ring buffer' 

. `dirname $0`/sharness.sh

test_under_flux 4 minimal

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
test_expect_success 'flux dmesg -C clears ring buffer' '
	flux logger --priority test.debug hello_dmesg &&
	flux dmesg | grep -q hello_dmesg &&
	flux dmesg -C &&
	! flux dmesg | grep -q hello_dmesg
'
test_expect_success 'flux setattr log-ring-size trims ring buffer' '
	OLD_RINGSIZE=`flux getattr log-ring-size` &&
	flux logger --priority test.debug hello1 &&
	flux logger --priority test.debug hello2 &&
	flux logger --priority test.debug hello3 &&
	flux logger --priority test.debug hello4 &&
	flux logger --priority test.debug hello5 &&
	flux logger --priority test.debug hello6 &&
	flux setattr log-ring-size 4 &&
	test `flux dmesg | wc -l` -eq 4 &&
	! flux dmesg | grep -q hello_dmesg1 &&
	! flux dmesg | grep -q hello_dmesg2 &&
	flux setattr log-ring-size $OLD_RINGSIZE
'
test_expect_success 'flux dmesg prints, no clear' '
	flux logger --priority test.debug hello_dmesg_pnc &&
	flux dmesg | grep -q hello_dmesg_pnc &&
	flux dmesg | grep -q hello_dmesg_pnc
'
test_expect_success 'flux dmesg -c prints and clears' '
	flux logger --priority test.debug hello_dmesg_pc &&
	flux dmesg -c | grep -q hello_dmesg_pc &&
	! flux dmesg | grep -q hello_dmesg_pc
'
test_expect_success 'ring buffer wraps over old entries' '
	OLD_RINGSIZE=`flux getattr log-ring-size` &&
	flux setattr log-ring-size 2 &&
	flux logger --priority test.debug hello_wrap1 &&
	flux logger --priority test.debug hello_wrap2 &&
	flux logger --priority test.debug hello_wrap3 &&
	! flux dmesg | grep -q hello_wrap1
	flux setattr log-ring-size $OLD_RINGSIZE
'

test_done
