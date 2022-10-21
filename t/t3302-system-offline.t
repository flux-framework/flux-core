#!/bin/sh
#

test_description='Test job submission when upstream is down

Start up only the leader broker and a leaf node, but not the
router node between them to simulate a login node without its
upstream router.  Try to submit a job on the leaf node and
confirm that the user gets a reasonable error.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_TOPO=kary:1
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

test_expect_success 'flux uptime on rank 2 reports join state' '
	bash -c \
		"FLUX_URI=$(echo $FLUX_URI | sed s/local-0/local-2/) \
			flux uptime" >uptime.out &&
	grep join uptime.out
'

test_done
