#!/bin/sh

test_description='Test broker log ring buffer' 

. `dirname $0`/sharness.sh

test_under_flux 4 minimal

test_expect_success 'flux getattr log-count counts log messages' '
	OLD_VAL=`flux getattr log-count` &&
	flux logger hello &&
	NEW_VAL=`flux getattr log-count` &&
	test "${OLD_VAL}" -lt "${NEW_VAL}"
'
test_expect_success 'flux getattr log-ring-used counts log messages' '
	OLD_VAL=`flux getattr log-ring-used` &&
	flux logger hello &&
	NEW_VAL=`flux getattr log-ring-used` &&
	test "${OLD_VAL}" -lt "${NEW_VAL}"
'
test_expect_success 'flux dmesg -C clears ring buffer' '
	flux logger hello_dmesg &&
	flux dmesg | grep -q hello_dmesg &&
	flux dmesg -C &&
	! flux dmesg | grep -q hello_dmesg
'
test_expect_success 'flux setattr log-ring-size trims ring buffer' '
	OLD_RINGSIZE=`flux getattr log-ring-size` &&
	flux logger hello1 &&
	flux logger hello2 &&
	flux logger hello3 &&
	flux logger hello4 &&
	flux logger hello5 &&
	flux logger hello6 &&
	flux setattr log-ring-size 4 &&
	test `flux dmesg | wc -l` -eq 4 &&
	! flux dmesg | grep -q hello_dmesg1 &&
	! flux dmesg | grep -q hello_dmesg2 &&
	flux setattr log-ring-size $OLD_RINGSIZE
'
test_expect_success 'flux dmesg prints, no clear' '
	flux logger hello_dmesg_pnc &&
	flux dmesg | grep -q hello_dmesg_pnc &&
	flux dmesg | grep -q hello_dmesg_pnc
'
test_expect_success 'flux dmesg -c prints and clears' '
	flux logger hello_dmesg_pc &&
	flux dmesg -c | grep -q hello_dmesg_pc &&
	! flux dmesg | grep -q hello_dmesg_pc
'
test_expect_success 'ring buffer wraps over old entries' '
	OLD_RINGSIZE=`flux getattr log-ring-size` &&
	flux setattr log-ring-size 2 &&
	flux logger hello_wrap1 &&
	flux logger hello_wrap2 &&
	flux logger hello_wrap3 &&
	! flux dmesg | grep -q hello_wrap1 &&
	flux setattr log-ring-size $OLD_RINGSIZE
'

test_expect_success 'multi-line log messages are split' '
	seq 1 8 | flux logger --appname=linesplit1 &&
	test $(flux dmesg | grep linesplit1 | wc -l) -eq 8
'

test_expect_success 'trailing cr/lf are stripped' '
	/bin/echo -n -e "xxx\x0A\x0D\x0A" | flux logger --appname striptest &&
	flux dmesg|grep striptest | sed -e "s/.*: //" >striptest.out &&
	echo "xxx" >striptest.exp &&
	test_cmp striptest.exp striptest.out
'

test_expect_success 'embedded blank log messages are ignored' '
	cat <<-EOT
	foo
	bar
	baz

	nerp
	EOT | flux logger --appname=embtest &&
	test $(flux dmesg | grep linesplit1 | wc -l) -eq 4
'

# Try to make flux dmesg get an EPROTO error
test_expect_success 'logged non-ascii characters handled ok' '
	/bin/echo -n -e "\xFF\xFE\x82\x00" | flux logger &&
	flux dmesg
'

test_done
