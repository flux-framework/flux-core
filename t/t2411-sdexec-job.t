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
if ! test_flux_security_version 0.14.0; then
	skip_all="requires flux-security >= v0.14, got ${FLUX_SECURITY_VERSION}"
	test_done
fi

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
sdexec-debug = true
[exec]
service-override = true
EOT

test_under_flux 2 full --config-path=$(pwd)/config

flux exec flux setattr log-stderr-level 7

sdexec="flux exec --service sdexec"
lptest="flux lptest"
rkill="flux sproc kill -s sdexec"

test_expect_success 'job gets exception if sdexec requested but not loaded' '
	test_must_fail flux run --setattr system.exec.bulkexec.service=sdexec \
	    -N1 true 2>except.err &&
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
	    -N1 true
'
test_expect_success '1-node job works' '
	flux run --setattr system.exec.bulkexec.service=sdexec \
	    -N1 true
'
test_expect_success 'dump broker logs' '
        flux dmesg >dmesg.out
'
test_expect_success '2-node job works' '
	flux run --setattr system.exec.bulkexec.service=sdexec \
	    -N2 true
'
test_expect_success 'create a shell userrc that dumps data to stderr' '
	cat >userrc.lua <<-EOT
	for i = 1,10,1
	do
	    io.stderr:write("foo bar\n")
	end
	EOT
'
test_expect_success 'run a job that uses that userrc' '
	flux run --setattr system.exec.bulkexec.service=sdexec \
	    -o userrc=userrc.lua true
'

select_log() {
	jq -r '. | select(.name=="log") | .name'
}
test_expect_success 'shell stderr includes 10 distinct lines of data' '
	flux job eventlog -f json -p exec $(flux job last) | select_log >logs &&
	count=$(wc -l <logs) &&
	test $count -eq 10
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

flux exec flux setattr log-stderr-level 1

test_done
