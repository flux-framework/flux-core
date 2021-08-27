#!/bin/sh
#

test_description='Test overlay parent disconnect'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_FANOUT=2

test_under_flux 15 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

test_expect_success 'tell each broker to log to stderr' '
	flux exec flux setattr log-stderr-mode local
'
test_expect_success 'flux overlay parentof fails with missing RANK' '
	test_must_fail flux overlay parentof
'
test_expect_success 'flux overlay parentof fails with rank out of range' '
	test_must_fail flux overlay parentof 42
'
test_expect_success 'flux overlay parentof fails with rank 0' '
	test_must_fail flux overlay parentof 0
'
test_expect_success 'flux overlay parentof relects k=2 topology' '
	test $(flux overlay parentof 1) -eq 0 &&
	test $(flux overlay parentof 2) -eq 0 &&
	test $(flux overlay parentof 3) -eq 1 &&
	test $(flux overlay parentof 4) -eq 1 &&
	test $(flux overlay parentof 5) -eq 2 &&
	test $(flux overlay parentof 6) -eq 2 &&
	test $(flux overlay parentof 7) -eq 3 &&
	test $(flux overlay parentof 8) -eq 3 &&
	test $(flux overlay parentof 9) -eq 4 &&
	test $(flux overlay parentof 10) -eq 4 &&
	test $(flux overlay parentof 11) -eq 5 &&
	test $(flux overlay parentof 12) -eq 5 &&
	test $(flux overlay parentof 13) -eq 6 &&
	test $(flux overlay parentof 14) -eq 6
'

test_expect_success 'flux overlay disconnect fails with missing rank' '
	test_must_fail flux overlay disconnect
'

test_expect_success 'flux overlay disconnect fails on incorrect parent' '
	test_must_fail flux overlay disconnect --parent 0 14
'

test_expect_success 'construct FLUX_URI for rank 13 (child of 6)' '
	echo "local://$(flux getattr rundir)/local-13" >uri13 &&
	test $(FLUX_URI=$(cat uri13) flux getattr rank) -eq 13
'

test_expect_success NO_CHAIN_LINT 'start background RPC to rank 0 via 13' '
	FLUX_URI=$(cat uri13) flux overlay status --wait=lost 2>health.err &
	echo $! >health.pid
'
test_expect_success 'ensure background request was received on rank 0' '
        FLUX_URI=$(cat uri13) flux overlay status
'

test_expect_success 'disconnect rank 6' '
	flux overlay disconnect 6
'
test_expect_success 'rank 6 exited with rc=1' '
	test_expect_code 1 $startctl wait 6
'
test_expect_success 'rank 13 exited' '
	($startctl wait 13 || /bin/true)
'
test_expect_success 'rank 14 exited' '
	($startctl wait 14 || /bin/true)
'

test_expect_success NO_CHAIN_LINT 'background RPC fails with overlay disconnect (tracker response from 6)' '
        pid=$(cat health.pid) &&
        echo waiting for pid $pid &&
        test_expect_code 1 wait $pid &&
        grep "overlay disconnect" health.err
'

test_expect_success 'report health status' '
	flux overlay status -vvv --pretty --ghost --color
'
test_expect_success 'health status for rank 6 is lost' '
	echo "6: lost" >status.exp &&
	flux overlay status -v >status.out &&
	test_cmp status.exp status.out
'

test_expect_success 'attempt to disconnect rank 6 again fails' '
	test_must_fail flux overlay disconnect 6
'

test_done
