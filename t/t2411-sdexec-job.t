#!/bin/sh
# ci=system

test_description='Test sdexec job launch'

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

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
sdbus-debug = true
sdexec-debug = true
[exec]
service-override = true
EOT

test_under_flux 2 full -o,--config-path=$(pwd)/config

flux setattr log-stderr-level 3

sdexec="flux exec --service sdexec"
lptest=${FLUX_BUILD_DIR}/t/shell/lptest
rkill="flux python ${SHARNESS_TEST_SRCDIR}/scripts/rexec.py kill -s sdexec"

test_expect_success 'job gets exception if sdexec requested but not loaded' '
	test_must_fail flux run --setattr system.exec.bulkexec.service=sdexec \
	    -N1 /bin/true 2>except.err &&
	grep "sdexec service is not loaded" except.err
'
test_expect_success 'load sdbus,sdexec modules' '
	flux exec flux module load sdbus &&
	flux exec flux module load sdexec
'
test_expect_success 'clear broker logs' '
        flux dmesg -C
'
test_expect_success 'incorrect bulkexec.service fails' '
	test_must_fail flux run --setattr system.exec.bulkexec.service=zzz \
	    -N1 /bin/true
'
test_expect_success '1-node job works' '
	flux run --setattr system.exec.bulkexec.service=sdexec \
	    -N1 /bin/true
'
test_expect_success 'dump broker logs' '
        flux dmesg >dmesg.out
'
test_expect_success '2-node job works' '
	flux run --setattr system.exec.bulkexec.service=sdexec \
	    -N2 /bin/true
'
test_expect_success 'remove sdexec,sdbus modules' '
	flux exec flux module remove sdexec &&
	flux exec flux module remove sdbus
'
test_expect_success 'remove job-exec module' '
	flux module remove job-exec
'
test_expect_success 'reconfigure with exec.sdexec-properties not a table' '
	flux config load <<-EOT
	exec.sdexec-properties = 42
	EOT
'
test_expect_success 'cannot load job-exec module' '
	test_must_fail flux module load job-exec
'
test_expect_success 'reconfigure with exec.sdexec-properties.key not a string' '
	flux config load <<-EOT
	[exec.sdexec-properties]
	foo = 24
	EOT
'
test_expect_success 'cannot load job-exec module' '
	test_must_fail flux module load job-exec
'

test_done
