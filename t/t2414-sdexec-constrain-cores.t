#!/bin/sh
# ci=system
test_description='Test sdexec cgroups cpuset controller manipulation

Test that Flux can restrict jobs to their allocated cores via the
AllowedCPUs systemd unit property when exec.sdexec-constrain-cores is
enabled.  The cpuset cgroup controller is used to verify the constraint.

See also:
https://www.freedesktop.org/software/systemd/man/systemd.resource-control.html
https://docs.kernel.org/admin-guide/cgroup-v2.html#cpuset
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
if ! systemctl show user@$(id -u) -p DelegateControllers | grep cpuset; then
	skip_all="cgroups cpuset controller is not delegated"
	test_done
fi
if ! test_flux_security_version 0.14.0; then
	skip_all="requires flux-security >= v0.14, got ${FLUX_SECURITY_VERSION}"
	test_done
fi

mkdir -p config
cat >config/config.toml <<EOT
[systemd]
enable = true
sdexec-debug = true
[exec]
service = "sdexec"
sdexec-constrain-cores = true
EOT

cat >getcg.sh <<'EOT2'
#!/bin/sh
cat $(flux cgroup path)/$1
EOT2
chmod +x getcg.sh

# Count CPUs in cpuset list notation, e.g. "0-3,8-11" -> 8
cat >count_cpus.py <<'EOT3'
import sys
from flux.idset import IDset
print(len(IDset(sys.stdin.read().strip())))
EOT3

test_under_flux 2 full --config-path=$(pwd)/config

getcg=$(pwd)/getcg.sh

# The cpuset controller is only written to cpuset.subtree_control by systemd
# when a unit first requests AllowedCPUs, so cgroup.controllers won't list it
# yet. Probe by running a job with an explicit AllowedCPUs and checking that
# cpuset.cpus.effective is readable:
if ! flux exec --service sdexec \
        --setopt=SDEXEC_PROP_AllowedCPUs=0 \
        $getcg cpuset.cpus.effective >/dev/null 2>&1; then
	skip_all="cpuset cgroup controller not functional in sdexec units"
	test_done
fi

ncores=$(flux resource list -i 0 -s free -n -o '{ncores}')
test "${ncores:-0}" -gt 2 && test_set_prereq MULTICORE

test_expect_success 'exec.sdexec-constrain-cores is set' '
	flux config get exec.sdexec-constrain-cores &&
	test "$(flux config get exec.sdexec-constrain-cores)" = "true" &&
	flux module stats job-exec \
	  | jq -e ".\"bulk-exec\".config.sdexec_constrain_cores == 1"
'

#
# basic: cpuset.cpus.effective is set and non-empty for a constrained job
#
test_expect_success 'cpuset.cpus.effective is set for a 1-core job' '
	flux run -n1 -c1 $getcg cpuset.cpus.effective >cpus1core.out &&
	test_debug "echo 1-core cpuset: $(cat cpus1core.out)" &&
	test -s cpus1core.out
'

#
# With MULTICORE: more cores -> more CPUs in cpuset
#
test_expect_success MULTICORE '2-core job cpuset contains more CPUs than 1-core job' '
	flux run -n1 -c2 $getcg cpuset.cpus.effective >cpus2core.out &&
	test_debug "echo 2-core cpuset: $(cat cpus2core.out)" &&
	n1=$(python3 $(pwd)/count_cpus.py <cpus1core.out) &&
	n2=$(python3 $(pwd)/count_cpus.py <cpus2core.out) &&
	test $n2 -gt $n1
'

#
# With MULTICORE: multiple nodes
#
test_expect_success MULTICORE 'multinode, multicore job works' '
	flux run -n2 -c2 -N2 --label-io \
	    $getcg cpuset.cpus.effective >multinode.out &&
	test_debug "cat multinode.out" &&
	sed -n s/^0://p multinode.out >node0.out &&
	sed -n s/^1://p multinode.out >node1.out &&
	flux job info $(flux job last) R > R.multinode.json &&
	count0=$(flux R decode --include=0 --count=core < R.multinode.json) &&
	test_debug "echo count0=$count0" &&
	count1=$(flux R decode --include=1 --count=core < R.multinode.json) &&
	test_debug "echo count1=$count1" &&
	n0=$(python3 $(pwd)/count_cpus.py <node0.out) &&
	n1=$(python3 $(pwd)/count_cpus.py <node1.out) &&
	test_debug "echo got $n0 cores on node0, expected $count0" &&
	test_debug "echo got $n1 cores on node1, expected $count1" &&
	test $count0 -eq $n0 &&
	test $count1 -eq $n1
'

test_expect_success MULTICORE 'all-cores job cpuset covers all system CPUs' '
	flux run -n1 -c${ncores} $getcg cpuset.cpus.effective >cpusall.out &&
	test_debug "echo all-core cpuset: $(cat cpusall.out)" &&
	nall=$(python3 $(pwd)/count_cpus.py <cpusall.out) &&
	n1=$(python3 $(pwd)/count_cpus.py <cpus1core.out) &&
	test $nall -gt $n1
'

#
# Reload config: disable sdexec-constrain-cores, verify constraint removed
#
# N.B. do these tests last as they alter the job-exec module config
#

test_expect_success 'disable sdexec-constrain-cores via config reload' '
	cat >config/config.toml <<-EOT &&
	[systemd]
	enable = true
	sdexec-debug = true
	[exec]
	service = "sdexec"
	EOT
	flux config reload
'

cat >getcpus.sh <<'EOF'
#!/bin/sh
awk '/^Cpus_allowed_list/ {print $2}' /proc/self/status
EOF
chmod +x getcpus.sh

# Note: without sdexec-constrain-cores, cpuset.cpus.effective may not exist
# in the job cgroup, so use Cpus_allowed_list in the following test:
test_expect_success MULTICORE 'without sdexec-constrain-cores, 1-core job gets full system cpuset' '
	flux run -n1 -c1 -o cpu-affinity=off ./getcpus.sh \
	    > cpus1unconstrained.out &&
	test_debug \
	    "echo unconstrained 1-core cpuset: $(cat cpus1unconstrained.out)" &&
	./getcpus.sh >unconstrained.expected &&
	test_cmp unconstrained.expected cpus1unconstrained.out
'

test_expect_success 're-enable sdexec-constrain-cores via config reload' '
	cat >config/config.toml <<-EOT &&
	[systemd]
	enable = true
	sdexec-debug = true
	[exec]
	service = "sdexec"
	sdexec-constrain-cores = true
	EOT
	flux config reload
'

test_expect_success 'after re-enable, 1-core job cpuset is restricted again' '
	flux run -n1 -c1 $getcg cpuset.cpus.effective >cpus1re.out &&
	test_debug "echo re-enabled 1-core cpuset: $(cat cpus1re.out)" &&
	test_cmp cpus1core.out cpus1re.out
'

test_done
