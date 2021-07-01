#!/bin/sh
#

test_description='Test local connector opt'

. `dirname $0`/sharness.sh
SIZE=1
test_under_flux ${SIZE} minimal

HANDLE_UTIL=${FLUX_BUILD_DIR}/t/util/handle

test_expect_success 'getopt flux::owner works' '
	ID=$($HANDLE_UTIL getopt u32 flux::owner) &&
	test $ID -eq $(id -u)
'
test_expect_success 'getopt unknown key fails' '
	test_must_fail $HANDLE_UTIL badkey
'
test_expect_success 'getopt wrong size' '
	test_must_fail $HANDLE_UTIL getopt u8 flux::owner
'

test_done
