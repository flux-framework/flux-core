#!/bin/sh
#

test_description='Test the tools used to test the system instance.

Before we begin testing the system instance, verify that the
system test personality behaves as it should, and that the
startctl tool and flux-start support for its RPCs work as expected.
'

. `dirname $0`/sharness.sh

test_under_flux 3 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

overlay_connected_children() {
	flux python -c "import flux; print(flux.Flux().rpc(\"overlay.stats-get\",nodeid=0).get_str())" | jq -r '.["child-connected"]'
}

test_expect_success 'broker hostlist has fake hostlist' '
	echo "fake[0-2]" >hostlist.exp &&
	flux getattr hostlist >hostlist.out &&
	test_cmp hostlist.exp hostlist.out
'

test_expect_success 'startctl status works' '
	$startctl status
'

test_expect_success 'broker overlay shows 2 connected children' '
	test $(overlay_connected_children) -eq 2
'

test_expect_success 'overlay status is full' '
	test "$(flux overlay status --timeout=0 --summary)" = "full"
'

test_expect_success 'kill broker rank=2 with SIGTERM like systemd stop' '
	$startctl kill 2 15
'

test_expect_success 'broker exits with no error' '
	run_timeout 30 $startctl wait 2
'

test_expect_success 'startctl shows rank 2 pid is -1' '
	pid=$($startctl status | jq -r ".procs[2].pid") &&
	test "$pid" = "-1"
'

test_expect_success 'broker overlay shows 1 connected child' '
	test $(overlay_connected_children) -eq 1
'

test_expect_success 'wait for overlay status to be partial' '
	run_timeout 10 flux overlay status --timeout=0 --summary --wait partial
'

test_expect_success 'run broker rank=2' '
	$startctl run 2
'

test_expect_success 'wait for overlay status to be full' '
	run_timeout 10 flux overlay status --timeout=0 --summary --wait full
'

test_expect_success 'flux exec over all ranks works' '
	run_timeout 30 flux exec flux getattr rank
'

test_expect_success 'kill broker rank=2 with SIGKILL, broker exits with 128+9' '
	$startctl kill 2 9 &&
	test_expect_code 137 run_timeout 30 $startctl wait 2
'

# Ensure an EHOSTUNREACH is encountered to trigger connected state change.
test_expect_success 'ping to rank 2 fails' '
	test_must_fail flux ping 2
'

test_expect_success 'wait for overlay status to be degraded' '
	run_timeout 10 flux overlay status --summary --wait degraded
'

test_expect_success 'run broker rank=2' '
	$startctl run 2
'

test_expect_success 'wait for subtree to be full' '
	run_timeout 10 flux overlay status --summary --wait full
'

test_expect_success 'run broker rank=2 again fails' '
	test_must_fail $startctl run 2
'

test_expect_success 'run broker rank=42 fails' '
	test_must_fail $startctl run 42
'

test_expect_success 'kill broker rank=42 fails' '
	test_must_fail $startctl kill 42 15
'

test_expect_success 'wait broker rank=42 fails' '
	test_must_fail $startctl wait 42
'

test_expect_success 'dump broker logs from leader broker' '
	flux dmesg | grep "broker\..*\[0\]"
'

test_done
