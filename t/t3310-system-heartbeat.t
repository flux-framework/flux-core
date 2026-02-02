#!/bin/sh
#

test_description='Deprive a leaf node of heartbeats and make it sad'

. `dirname $0`/sharness.sh

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

test_under_flux 2 system -Slog-stderr-mode=local

test_expect_success 'ensure child is online' '
	flux overlay status --timeout=0 --wait full
'

test_expect_success 'configure heartbeat' '
	flux config load <<-EOT
	[heartbeat]
	period = "0.1s"
	timeout = "0.5s"
	EOT
'
test_expect_success 'stop heartbeat' '
	flux module remove heartbeat
'
test_expect_success 'wait for rank 1 to exit' '
	test_expect_code 1 $startctl wait 1
'
test_expect_success 'start heartbeat' '
	flux module load heartbeat
'
test_expect_success 'wait for degraded status' '
	flux overlay status --timeout=0 --wait=degraded
'

test_done
