#!/bin/sh

test_description='Test properties using systemd job execution service'

. $(dirname $0)/sharness.sh

if ! flux version | grep systemd; then
    skip_all='flux not built with systemd'
    test_done
fi

#  Don't run if systemd environment variables not setup
if test -z "$DBUS_SESSION_BUS_ADDRESS" \
     -o -z "$XDG_RUNTIME_DIR"; then
    skip_all='DBUS_SESSION_BUS_ADDRESS and/or XDG_RUNTIME_DIR not set'
    test_done
fi

uid=`id -u`
userservice="user@${uid}.service"
if ! systemctl list-units | grep ${userservice}
then
    skip_all='systemd user service not running'
    test_done
fi

export FLUX_CONF_DIR=$(pwd)
test_under_flux 1 job
flux setattr log-stderr-level 1

if ! which hwloc-bind > /dev/null; then
    skip_all='skipping sdexec affinity tests since hwloc-bind not found'
    test_done
fi

corecount=`hwloc-bind --get | hwloc-calc --number-of core | tail -n 1`
test ${corecount} = 1 || test_set_prereq MULTICORE
if mount | grep cgroup2
then
    test_set_prereq CGROUP2
fi

delegations=`systemctl show ${userservice} | grep DelegateControllers`
if echo ${delegations} | grep cpuset
then
    test_set_prereq CPUSET
fi
if echo ${delegations} | grep memory
then
    test_set_prereq MEMORY
fi

IDSET2BITMASK="flux python ${FLUX_SOURCE_DIR}/t/job-exec/idset2bitmask.py"

# arg1 jobid
jobid2unitname() {
    jobid=$1
    jobiddec=`flux job id --to=dec $jobid`
    rank=`flux getattr rank`
    echo "flux-sdexec-${rank}-${jobiddec}"
}

# arg1 - jobid
get_cgrouppath() {
    jobid=$1
    unitname=`jobid2unitname ${jobid}`
    cgroupsuffix=`systemctl show --property ControlGroup --user --value ${unitname}`
    cgrouppath="/sys/fs/cgroup/${cgroupsuffix}"
    echo $cgrouppath
}

# arg1 - jobid
get_cpuaffinity() {
    jobid=$1
    unitname=`jobid2unitname ${jobid}`
    mainpid=`systemctl show --property MainPID --user --value ${unitname}`
    cpuaffinity=`taskset -p ${mainpid} | awk '{print $NF}'`
    echo "0x${cpuaffinity}"
}

# arg1 - jobid
get_cpusallowed() {
    jobid=$1
    cgrouppath=`get_cgrouppath ${jobid}`
    cpuset=`cat ${cgrouppath}/cpuset.cpus`
    cpusallowed=`${IDSET2BITMASK} ${cpuset}`
    echo ${cpusallowed}
}

# arg1 - jobid
get_memoryhigh() {
    jobid=$1
    cgrouppath=`get_cgrouppath ${jobid}`
    memory=`cat ${cgrouppath}/memory.high`
    echo ${memory}
}

# arg1 - jobid
get_memorymax() {
    jobid=$1
    cgrouppath=`get_cgrouppath ${jobid}`
    memory=`cat ${cgrouppath}/memory.max`
    echo ${memory}
}

get_system_mem_bytes() {
    memkb=`cat /proc/meminfo | head -n 1 | awk '{print $2}'`
    membytes=$((memkb * 1024))
    echo $membytes
}

# arg1 - cpu bitmask
# arg2 - expected number of processor units with bitmask
validate_cpuset() {
    cpubitmask=$1
    expected=$2

    pu_count=`echo ${cpubitmask} | hwloc-calc --number-of pu | tail -n 1`
    if [ "${pu_count}" = "${expected}" ]
    then
        return 0
    fi
    # Possible pu_count > expected b/c of hyperthreading and similar
    # effects so check core count
    core_count=`echo ${cpubitmask} | hwloc-calc --number-of core | tail -n 1`
    if [ ${pu_count} > ${core_count} ] \
        && [ "${core_count}" = "${expected}" ]
    then
        return 0
    fi
    return 1
}

# arg1 - memory read
# arg2 - expected memory
validate_memory() {
    # there can be rounding errors, so we just try to make sure the
    # memory is within 1 percent of what is expected
    memory=$1
    expected=$2
    if [ "${expected}" = "infinity" ]
    then
        expected=`get_system_mem_bytes`
    fi
    expected_low=`echo "${expected} * 0.99" | bc`
    expected_high=`echo "${expected} * 1.01" | bc`
    if [ ${memory} -gt ${expected_low%.*} ] && [ ${memory} -lt ${expected_high%.*} ]
    then
        return 0
    fi
    return 1
}

# arg1 - memory read
# arg2 - percent
validate_memory_percent() {
    memory=$1
    percent=$2
    membytes=`get_system_mem_bytes`
    expected=`echo "${membytes} * ${percent}" | bc`
    validate_memory ${memory} ${expected%.*}
    return $?
}

# arg1 - memory read
# arg2 - memory expected
validate_memory_bytes() {
    memory=$1
    expected=$2
    validate_memory ${memory} ${expected}
    return $?
}

#
# CPUAffinity
#
test_expect_success 'job-exec: config sdexec and cpu_set_affinity' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"

[exec.systemd]
cpu_set_affinity = true
EOF
        flux config reload &&
        flux module reload job-exec
'
# the default flux job-shell sets CPU affinity as well, so we use a fake
# job shell to test CPU affinity settings via systemd
test_expect_success 'job-exec: create fake shell script' '
        cat >sleepshell.sh <<-EOF &&
#!/bin/bash
echo "fake shell sleeping"
sleep 60
EOF
        chmod +x sleepshell.sh
'
test_expect_success 'job-exec: cpu_set_affinity (1 core)' '
        jobid=$(flux submit -n1 \
                --setattr=system.exec.job_shell="$(pwd)/sleepshell.sh" \
                hostname) &&
        flux job wait-event -t 30 $jobid start &&
        cpubind=`get_cpuaffinity ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_cpuset ${cpubind} 1
'
test_expect_success MULTICORE 'job-exec: cpu_set_affinity (2 core)' '
        jobid=$(flux submit -n2 \
                --setattr=system.exec.job_shell="$(pwd)/sleepshell.sh" \
                hostname) &&
        flux job wait-event -t 30 $jobid start &&
        cpubind=`get_cpuaffinity ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_cpuset ${cpubind} 2
'
#
# AllowedCPUs
#
test_expect_success CGROUP2,CPUSET 'job-exec: config sdexec and cpu_set_allowed' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"

[exec.systemd]
cpu_set_allowed = true
EOF
        flux config reload &&
        flux module reload job-exec
'
test_expect_success CGROUP2,CPUSET 'job-exec: cpu_set_allowed (1 core)' '
        jobid=$(flux submit -n1 sleep 60) &&
        flux job wait-event -t 30 $jobid start &&
        cpusallowed=`get_cpusallowed ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_cpuset ${cpusallowed} 1
'
test_expect_success MULTICORE,CGROUP2,CPUSET 'job-exec: cpu_set_allowed (2 core)' '
        jobid=$(flux submit -n2 sleep 60) &&
        flux job wait-event -t 30 $jobid start &&
        cpusallowed=`get_cpusallowed ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_cpuset ${cpusallowed} 2
'
#
# MemoryHigh
# MemoryMax
#
test_expect_success CGROUP2,MEMORY 'job-exec: config sdexec with memory settings (bytes)' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"

[exec.systemd]
MemoryHigh = "1048576"
MemoryMax = "2097152"
EOF
        flux config reload &&
        flux module reload job-exec
'
test_expect_success CGROUP2,MEMORY 'job-exec: sdexec memory settings work (bytes)' '
        jobid=$(flux submit -n1 sleep 60) &&
        flux job wait-event -t 30 $jobid start &&
        memhigh=`get_memoryhigh ${jobid}` &&
        memmax=`get_memorymax ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_memory_bytes ${memhigh} 1048576 &&
        validate_memory_bytes ${memmax} 2097152
'
test_expect_success CGROUP2,MEMORY 'job-exec: config sdexec with memory settings (suffix)' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"

[exec.systemd]
MemoryHigh = "1024k"
MemoryMax = "2m"
EOF
        flux config reload &&
        flux module reload job-exec
'
test_expect_success CGROUP2,MEMORY 'job-exec: sdexec memory settings work (suffix)' '
        jobid=$(flux submit -n1 sleep 60) &&
        flux job wait-event -t 30 $jobid start &&
        memhigh=`get_memoryhigh ${jobid}` &&
        memmax=`get_memorymax ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_memory_bytes ${memhigh} 1048576 &&
        validate_memory_bytes ${memmax} 2097152
'
test_expect_success CGROUP2,MEMORY 'job-exec: config sdexec with memory settings (percent)' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"

[exec.systemd]
MemoryHigh = "80%"
MemoryMax = "90%"
EOF
        flux config reload &&
        flux module reload job-exec
'
test_expect_success CGROUP2,MEMORY 'job-exec: sdexec memory settings work (percent)' '
        jobid=$(flux submit -n1 sleep 60) &&
        flux job wait-event -t 30 $jobid start &&
        memhigh=`get_memoryhigh ${jobid}` &&
        memmax=`get_memorymax ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_memory_percent ${memhigh} 0.80 &&
        validate_memory_percent ${memmax} 0.90
'
test_expect_success CGROUP2,MEMORY 'job-exec: config sdexec with memory settings (infinity)' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"

[exec.systemd]
MemoryHigh = "infinity"
MemoryMax = "infinity"
EOF
        flux config reload &&
        flux module reload job-exec
'
test_expect_success CGROUP2,MEMORY 'job-exec: sdexec memory settings work (infinity)' '
        jobid=$(flux submit -n1 sleep 60) &&
        flux job wait-event -t 30 $jobid start &&
        memhigh=`get_memoryhigh ${jobid}` &&
        memmax=`get_memorymax ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_memory_bytes ${memhigh} "infinity" &&
        validate_memory_bytes ${memmax} "infinity"
'

test_done

