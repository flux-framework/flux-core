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

# arg1 jobid
jobid2unitname() {
    jobid=$1
    jobiddec=`flux job id --to=dec $jobid`
    rank=`flux getattr rank`
    echo "flux-sdexec-${rank}-${jobiddec}"
}

# arg1 - jobid
get_cpuaffinity() {
    jobid=$1
    unitname=`jobid2unitname ${jobid}`
    mainpid=`systemctl show --property MainPID --user --value ${unitname}`
    cpuaffinity=`taskset -p ${mainpid} | awk '{print $NF}'`
    echo "0x${cpuaffinity}"
}

# arg1 - cpubind info
# arg2 - expected number of processor units with affinity
validate_affinity() {
    cpubind=$1
    expected=$2

    pu_count=`echo ${cpubind} | hwloc-calc --number-of pu | tail -n 1`
    if [ "${pu_count}" = "${expected}" ]
    then
        return 0
    fi
    # Possible pu_count > expected b/c of hyperthreading and similar
    # effects so check core count
    core_count=`echo ${cpubind} | hwloc-calc --number-of core | tail -n 1`
    if [ ${pu_count} > ${core_count} ] \
        && [ "${core_count}" = "${expected}" ]
    then
        return 0
    fi
    return 1
}

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
        jobid=$(flux mini submit -n1 \
                --setattr=system.exec.job_shell="$(pwd)/sleepshell.sh" \
                hostname) &&
        flux job wait-event -t 30 $jobid start &&
        cpubind=`get_cpuaffinity ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_affinity ${cpubind} 1
'
test_expect_success MULTICORE 'job-exec: cpu_set_affinity (2 core)' '
        jobid=$(flux mini submit -n2 \
                --setattr=system.exec.job_shell="$(pwd)/sleepshell.sh" \
                hostname) &&
        flux job wait-event -t 30 $jobid start &&
        cpubind=`get_cpuaffinity ${jobid}` &&
        flux job cancel ${jobid} &&
        flux job wait-event -t 30 $jobid clean &&
        validate_affinity ${cpubind} 2
'

test_done

