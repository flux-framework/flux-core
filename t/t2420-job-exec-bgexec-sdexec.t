#!/bin/sh
# ci=system

test_description='Test job-exec bgexec implementation over the sdexec service

Exercises exec.method=bgexec combined with exec.service=sdexec, including
reattach to running jobs across a job-exec module reload.  bgexec launches
job shells as transient systemd units via sdexec; those units live in the
user systemd manager and survive a job-exec reload, so the job manager can
re-issue the start request with reattach set and bgexec recovers status by
re-waiting each rank by its deterministic label.  This mirrors the rexec
reattach coverage in t2418-job-exec-bgexec.t.
'

. $(dirname $0)/sharness.sh

if ! flux version | grep systemd; then
	skip_all="flux was not built with systemd"
	test_done
fi
if ! systemctl --user show --property Version; then
	skip_all="user systemd is not running"
	test_done
fi
if ! busctl --user status >/dev/null; then
	skip_all="user dbus is not running"
	test_done
fi
if ! test_flux_security_version 0.14.0; then
	skip_all="requires flux-security >= v0.14, got ${FLUX_SECURITY_VERSION}"
	test_done
fi

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
sdexec-debug = true
[exec]
method = "bgexec"
service = "sdexec"
EOT

test_under_flux 2 full --config-path=$(pwd)/config

# ---------------------------------------------------------------------------
# Setup: load sdbus/sdexec on all ranks, confirm bgexec is selected
# ---------------------------------------------------------------------------

test_expect_success 'load sdbus,sdexec modules on all ranks' '
	flux exec flux module load sdbus &&
	flux exec flux module load sdexec
'
test_expect_success 'job-exec selected method=bgexec from config' '
	flux module stats job-exec \
	    | jq -e ".method == \"bgexec\""
'

# ---------------------------------------------------------------------------
# Normal execution under bgexec+sdexec
# ---------------------------------------------------------------------------

test_expect_success 'single-node job runs to completion' '
	id=$(flux submit -N1 true) &&
	flux job wait-event -t 60 $id clean &&
	flux job status $id
'
test_expect_success 'multi-node job runs to completion' '
	id=$(flux submit -N2 -n2 true) &&
	flux job wait-event -t 60 $id clean &&
	flux job status $id
'
test_expect_success 'job exit status is propagated' '
	test_expect_code 1 flux run -N1 false
'
test_expect_success 'job stdout is captured' '
	flux run -N1 echo hello >stdout.out &&
	grep hello stdout.out
'

# ---------------------------------------------------------------------------
# reattach across a module reload
#
# The transient units launched via sdexec survive a job-exec reload.  On
# reload the job manager re-issues the start request with reattach set;
# bgexec recovers status by re-waiting each rank by its deterministic label
# rather than relaunching.
# ---------------------------------------------------------------------------

test_expect_success 'single-node job posts recoverable and reattaches on reload' '
	id=$(flux submit --flags=debug -N1 sleep 300) &&
	flux job wait-event -t 60 $id start &&
	flux job wait-event -p exec -t 60 $id recoverable &&
	flux module reload job-exec &&
	flux job wait-event -t 60 $id debug.exec-reattach-finish &&
	flux job eventlog $id >reattach1.out &&
	test_debug "cat reattach1.out" &&
	grep "debug.start-lost" reattach1.out &&
	grep "debug.exec-reattach-finish" reattach1.out &&
	test $(flux jobs -no "{state}" $id) = RUN &&
	flux cancel $id &&
	flux job wait-event -t 60 $id clean
'
test_expect_success 'multi-node job posts recoverable after second barrier' '
	id=$(flux submit --flags=debug -N2 -n2 sleep 300) &&
	flux job wait-event -t 60 $id start &&
	flux job wait-event -p exec -t 60 $id recoverable
'
test_expect_success 'multi-node job is reattached rather than relaunched' '
	flux module reload job-exec &&
	flux job wait-event -t 60 $id debug.exec-reattach-finish &&
	flux job eventlog $id >reattach2.out &&
	test_debug "cat reattach2.out" &&
	grep "debug.start-lost" reattach2.out &&
	grep "debug.exec-reattach-finish" reattach2.out
'
test_expect_success 'reattached multi-node job remains in RUN state' '
	test $(flux jobs -no "{state}" $id) = RUN
'
test_expect_success 'reattached multi-node job can be canceled and cleaned up' '
	flux cancel $id &&
	flux job wait-event -t 60 $id clean
'

# A reattached multi-node job that exits normally (rather than being
# canceled) must not be misread as having terminated before the first
# barrier.  The reattach path marks both barriers done since the recoverable
# event gate guarantees they completed in the prior incarnation.
test_expect_success 'reattached multi-node job exits cleanly without exception' '
	id=$(flux submit --flags=debug -N2 -n2 sleep 15) &&
	flux job wait-event -p exec -t 60 $id recoverable &&
	flux module reload job-exec &&
	flux job wait-event -t 60 $id debug.exec-reattach-finish &&
	flux job wait-event -t 60 $id clean &&
	flux job status $id &&
	test_must_fail flux job wait-event -t 5 $id exception
'

# ---------------------------------------------------------------------------
# cleanup
# ---------------------------------------------------------------------------

test_expect_success 'remove sdexec,sdbus modules' '
	flux exec flux module remove sdexec &&
	flux exec flux module remove sdbus
'

test_done
