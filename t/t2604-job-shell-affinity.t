#!/bin/sh
#
test_description='Test flux-shell default affinity implementation'

. `dirname $0`/sharness.sh

test_under_flux 2

jq=$(which jq 2>/dev/null)
test -z "$jq" || test_set_prereq HAVE_JQ

if ! which hwloc-bind > /dev/null; then
    skip_all='skipping affinity tests since hwloc-bind not found'
    test_done
fi

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"
CPUS_ALLOWED_COUNT="$(pwd)/cpus-allowed-count.sh"

cat >${CPUS_ALLOWED_COUNT} << EOF
#!/bin/sh
hwloc-bind --get | hwloc-calc --number-of core
EOF
chmod +x ${CPUS_ALLOWED_COUNT}

test $(${CPUS_ALLOWED_COUNT}) = 1 || test_set_prereq MULTICORE

echo >&2 "# Running tests on $($CPUS_ALLOWED_COUNT) cores"

test_expect_success 'flux-shell: affinity hwloc-calc works' '
    hwloc-bind --get &&
    hwloc-bind --get | hwloc-calc --number-of core &&
    hwloc-bind --get | hwloc-calc --number-of pu
'
test_expect_success 'flux-shell: default affinity works (1 core)' '
    flux srun -n1 -c1 $CPUS_ALLOWED_COUNT > result.n1 &&
    test_debug "cat result.n1" &&
    test "$(cat result.n1)" = "1"
'
test_expect_success MULTICORE 'flux-shell: default affinity works (2 cores)' '
    flux srun -n1 -c2 $CPUS_ALLOWED_COUNT > result.n1 &&
    test_debug "cat result.n1" &&
    test "$(cat result.n1)" = "2"
'
test_expect_success HAVE_JQ,MULTICORE 'flux-shell: per-task affinity works' '
    jobid=$(flux jobspec srun -n2 -c1 hwloc-bind --get \
     | $jq ".attributes.system.shell.options[\"cpu-affinity\"] = \"per-task\"" \
     | flux job submit) &&
    flux job attach --label ${jobid} > per-task.out &&
    task0set=$(sed -n "s/^0: //p" per-task.out) &&
    task1set=$(sed -n "s/^1: //p" per-task.out) &&
    test_debug "echo checking ${task0set} not equal ${task1set}" &&
    test "$task0set" != "$task1set"
'
test_expect_success HAVE_JQ 'flux-shell: per-task affinity sanity check' '
    jobid=$(flux jobspec srun -n1 -c1 hwloc-bind --get \
     | $jq ".attributes.system.shell.options[\"cpu-affinity\"] = \"per-task\"" \
     | flux job submit) &&
    flux job attach --label ${jobid}
'
test_expect_success HAVE_JQ 'flux-shell: affinity can be disabled' '
     hwloc-bind --get > affinity-off.expected &&
    jobid=$(flux jobspec srun -n1 hwloc-bind --get \
     | $jq ".attributes.system.shell.options[\"cpu-affinity\"] = \"off\"" \
     | flux job submit) &&
    flux job attach ${jobid} > affinity-off.out &&
    test_cmp affinity-off.expected affinity-off.out
'
test_expect_success HAVE_JQ 'flux-shell: invalid option is ignored' '
    jobid=$(flux jobspec srun -n1 hwloc-bind --get \
     | $jq ".attributes.system.shell.options[\"cpu-affinity\"] = 1" \
     | flux job submit) &&
    flux job attach ${jobid} &&
    flux dmesg | grep "invalid option"
'
flux dmesg
flux dmesg | grep 'unable to get cpuset'
test_done
