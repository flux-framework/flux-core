#!/bin/sh
# ci=system

test_description='Test that prolog and epilog can be run under systemd'

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
enable = true
sdbus-debug = true
sdexec-debug = true

[exec]
service = "sdexec"

[job-manager]
plugins = [
    { load = "perilog.so" }
]
prolog.command = [
   "flux", "perilog-run", "prolog", "--sdexec", "-e", "$(pwd)/flist.sh"
]
epilog.command = [
   "flux", "perilog-run", "epilog", "--sdexec", "-e", "$(pwd)/flist.sh"
]
EOT

test_under_flux 1 full -o,--config-path=$(pwd)/config

flux setattr log-stderr-level 1

test_expect_success 'create flist.sh script' '
	cat >flist.sh<<-EOT &&
	#!/bin/sh
	systemctl --user list-units --type=service | grep "active running"
	EOT
	chmod +x flist.sh
'
test_expect_success 'run a single node job and capture broker logs' '
	flux dmesg -C &&
	flux run --wait-event=clean -vvv -N1 ./flist.sh >run.out &&
	flux dmesg >dmesg.out
'
# Search for unit descriptions in list-units output as an indication that
# the task ran under systemd.  These unit descriptions must match what is set
# in job-exec and flux-perilog.py.
test_expect_success 'shell was run under systemd' '
	grep "User workload" run.out
'
test_expect_success 'prolog was run under systemd' '
	grep "System prolog script" dmesg.out
'
test_expect_success 'epilog was run under systemd' '
	grep "System epilog script" dmesg.out
'
test_done
