#!/bin/sh
#

test_description='Test system instance with one offline router broker'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_FANOUT=1
export TEST_UNDER_FLUX_QUORUM=0
export TEST_UNDER_FLUX_START_MODE=leader

test_under_flux 3 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

test_expect_success 'start rank 2 before TBON parent is up' '
	$startctl run 2
'

test_expect_success HAVE_JQ 'startctl shows rank 1 pid as -1' '
        test $($startctl status | jq -r ".procs[1].pid") = "-1"
'

test_expect_success 'flux mini run on rank 2 fails with offline error' '
	test_must_fail bash -c \
		"FLUX_URI=$(echo $FLUX_URI | sed s/local-0/local-2/) \
			flux mini run hostname" 2>online.err &&
	grep "broker is offline" online.err
'

test_done
