#!/bin/sh
#

test_description='Test overlay parent disconnect'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_FANOUT=2

test_under_flux 15 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

test_expect_success 'flux overlay parentof fails with missing RANK' '
	test_must_fail flux overlay parentof
'
test_expect_success 'flux overlay parentof fails with rank out of range' '
	test_must_fail flux overlay parentof 42
'
test_expect_success 'flux overlay parentof fails with rank 0' '
	test_must_fail flux overlay parentof 0
'

test_done
