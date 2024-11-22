#!/bin/sh
# ci=system
test_description='Test sdexec cgroups memory controller manipulation

Test whether Flux can affect the cgroups memory controller for processes
spawned via sdexec, and the capability to configure basic limits for jobs.

See also:
https://www.freedesktop.org/software/systemd/man/systemd.resource-control.html
https://docs.kernel.org/admin-guide/cgroup-v2.html#memory
cgroups(7) /proc/[pid]/cgroup
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
if ! systemctl show user@$(id -u) -p DelegateControllers | grep memory; then
	skip_all="cgroups memory controller is not delegated"
	test_done
fi
if stress=$(which stress); then
	test_set_prereq STRESS
fi

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
enable = true
sdexec-debug = true
[exec]
service = "sdexec"
[exec.sdexec-properties]
MemoryHigh = "200M"
MemoryMax = "100M"
EOT

cat >getcg.sh <<EOT2
#!/bin/sh
CGDIR=/sys/fs/cgroup/\$(cat /proc/\$\$/cgroup | cut -d: -f3)
cat \$CGDIR/\$1
EOT2
chmod +x getcg.sh

if ! $(pwd)/getcg.sh cgroup.controllers | grep memory; then
	skip_all="cgroups memory controller is not enabled"
	test_done
fi

test_under_flux 1 full --config-path=$(pwd)/config

flux setattr log-stderr-level 7

sdexec="flux exec --service sdexec"
getcg=$(pwd)/getcg.sh

if ! $sdexec $getcg memory.high; then
	skip_all="$sdexec getcg doesn't work"
	test_done
fi

#
# memory.high: the throttling limit on memory usage
#
test_expect_success 'memory.high exists' '
	$sdexec $getcg memory.high
'
test_expect_success 'memory.high can be set to 200M' '
	cat >200M.exp <<-EOT &&
	209715200
	EOT
	$sdexec \
	    --setopt=SDEXEC_PROP_MemoryHigh=200M \
	    $getcg memory.high >high.out &&
	test_cmp 200M.exp high.out
'
test_expect_success 'memory.high can be set to infinity' '
	cat >inf.exp <<-EOT &&
	max
	EOT
	$sdexec \
	    --setopt=SDEXEC_PROP_MemoryHigh=infinity \
	    $getcg memory.high >high2.out &&
	test_cmp inf.exp high2.out
'
test_expect_success 'memory.high can be configured for jobs' '
	flux run $getcg memory.high >high3.out &&
	test_cmp 200M.exp high3.out
'

#
# memory.max: the absolute limit on memory usage
#
test_expect_success 'memory.max exists' '
	$sdexec $getcg memory.max
'
test_expect_success 'memory.max can be set to 100M' '
	cat >100M.exp <<-EOT &&
	104857600
	EOT
	$sdexec \
	    --setopt=SDEXEC_PROP_MemoryMax=100M \
	    $getcg memory.max >max.out &&
	test_cmp 100M.exp max.out
'
test_expect_success 'memory.max can be set to infinity' '
	cat >inf.exp <<-EOT &&
	max
	EOT
	$sdexec \
	    --setopt=SDEXEC_PROP_MemoryMax=infinity \
	    $getcg memory.max >max2.out &&
	test_cmp inf.exp max2.out
'
test_expect_success 'memory.max can be configured for jobs' '
	flux run $getcg memory.max >max3.out &&
	test_cmp 100M.exp max3.out
'
test_expect_success STRESS 'remaining within memory.max works' '
	$sdexec \
	    --setopt=SDEXEC_PROP_MemoryMax=200M \
	    $stress --timeout 3 --vm-keep --vm 1 --vm-bytes 100M
'

# Like test_expect_code() except 143/SIGKILL (oom kill) is also acceptable
test_expect_code_or_killed() {
	local want_code=$1
	shift
	"$@"
	exit_code=$?
	if test $exit_code = $want_code || test $exit_code = 143; then
	    return 0
	fi
	echo >&2 "test_expect_code_or_killed: command exited with $exit_code, we wanted $want_code or 143/SIGKILL $*"
	return 1
}

test_expect_success STRESS 'exceeding memory.max causes exec failure' '
	test_expect_code_or_killed 1 $sdexec \
	    --setopt=SDEXEC_PROP_MemoryMax=100M \
	    $stress --timeout 60 --vm-keep --vm 1 --vm-bytes 200M
'
test_expect_success STRESS 'exceeding memory.max causes job failure' '
	test_expect_code_or_killed 1 flux run \
	    $stress --timeout 60 --vm-keep --vm 1 --vm-bytes 200M
'

#
# reload / update config
#
# N.B. do these tests last, as they will alter the config of the
# job-exec module and could affect above tests
#

test_expect_success 'change values of memory containment' '
	cat >config/config.toml <<-EOT &&
	[systemd]
	enable = true
	sdexec-debug = true
	[exec]
	service = "sdexec"
	[exec.sdexec-properties]
	MemoryHigh = "100M"
	MemoryMax = "infinity"
	EOT
	flux config reload
'
test_expect_success 'memory.high configuration changed' '
	flux run $getcg memory.high >highupdate.out &&
	test_cmp 100M.exp highupdate.out
'
test_expect_success 'memory.max configuration changed' '
	flux run $getcg memory.max >maxupdate.out &&
	test_cmp inf.exp maxupdate.out
'

flux setattr log-stderr-level 3

test_done
