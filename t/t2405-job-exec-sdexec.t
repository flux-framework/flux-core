#!/bin/sh
# ci=system

test_description='Test flux job execution service using systemd'

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

TEST_SDPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsdprocess

test_expect_success 'job-exec: basic test sdexec works' '
        jobid=$(flux submit \
                --setattr=system.exec.sd.test=true \
                sleep 30) &&
        flux job wait-event -t 10 ${jobid} start &&
        jobiddec=`flux job id --to=dec $jobid` &&
        rank=`flux getattr rank` &&
        systemctl list-units --user | grep "flux-sdexec-${rank}-${jobiddec}" &&
        flux cancel ${jobid} &&
        flux job wait-event -t 10 ${jobid} clean
'

test_expect_success 'job-exec: config sdexec as executor' '
        cat >exec.toml <<EOF &&
[exec]
method = "systemd"
EOF
        flux config reload &&
        flux module reload job-exec
'

test_expect_success 'job-exec: basic sdexec with exec config works' '
        jobid=$(flux submit sleep 30) &&
        flux job wait-event -t 10 ${jobid} start &&
        jobiddec=`flux job id --to=dec $jobid` &&
        rank=`flux getattr rank` &&
        systemctl list-units --user | grep "flux-sdexec-${rank}-${jobiddec}" &&
        flux cancel ${jobid} &&
        flux job wait-event -t 10 ${jobid} clean
'

test_expect_success 'job-exec: generate jobspec for simple test job' '
        flux run --dry-run hostname > basic.json
'
test_expect_success 'job-exec: basic job runs through all states under systemd' '
        jobid=$(flux job submit basic.json) &&
        flux job wait-event -t 10 ${jobid} start &&
        flux job wait-event -t 10 ${jobid} finish &&
        flux job wait-event -t 10 ${jobid} release &&
        flux job wait-event -t 10 ${jobid} clean
'
test_expect_success 'job-exec: simple job outputs stdout under systemd' '
        jobid=$(flux submit ${TEST_SDPROCESS_DIR}/test_echo -O foobar) &&
        flux job attach $jobid 1> stdout.out &&
        echo foobar > stdout.expected &&
        test_cmp stdout.expected stdout.out
'
test_expect_success 'job-exec: simple job outputs stderr under systemd' '
        jobid=$(flux submit ${TEST_SDPROCESS_DIR}/test_echo -E boobar) &&
        flux job attach $jobid 2> stderr.out &&
        echo boobar > stderr.expected &&
        test_cmp stderr.expected stderr.out
'
test_expect_success 'job-exec: simple job takes stdin under systemd' '
        jobid=$(flux submit ${TEST_SDPROCESS_DIR}/test_echo -O) &&
        echo -n "boobaz" | flux job attach $jobid 1> stdout.out &&
        echo boobaz > stdout.expected &&
        test_cmp stdout.expected stdout.out
'
test_expect_success 'job-exec: simple job exits 0 on success' '
        jobid=$(flux submit /bin/true) &&
        test_expect_code 0 flux job status $jobid
'
test_expect_success 'job-exec: simple job exits 1 on failure' '
        jobid=$(flux submit /bin/false) &&
        test_expect_code 1 flux job status -vv $jobid
'
test_expect_success 'job-exec: simple job exits 127 on bad command' '
        jobid=$(flux submit /bin/foobar) &&
        test_expect_code 127 flux job status $jobid
'
test_expect_success 'job-exec: simple job exits 138 on signaled job' '
        jobid=$(flux submit sleep 30) &&
        flux job wait-event -t 10 ${jobid} start &&
        flux job kill --signal=SIGUSR1 $jobid &&
        test_expect_code 138 flux job status $jobid
'
test_expect_success 'job-exec: simple job can be canceled' '
        jobid=$(flux submit sleep 30) &&
        flux job wait-event -t 10 ${jobid} start &&
        flux cancel $jobid &&
        flux job wait-event -t 10 ${jobid} clean &&
        test_expect_code 143 flux job status $jobid
'
test_expect_success 'job-exec: job fails correctly if user service not setup' '
        jobid=$(flux submit \
                --setattr=system.exec.sd.test_exec_fail=true \
                hostname) &&
        flux job wait-event -v -t 30 $jobid clean &&
        flux job eventlog ${jobid} | grep "test sdprocess_exec" \
             | grep "Operation not permitted"
'
test_expect_success 'job-exec: systemd cleaned up after job completes' '
        jobid=$(flux submit sleep 30) &&
        flux job wait-event -v -t 30 $jobid start &&
        jobiddec=`flux job id --to=dec $jobid` &&
        rank=`flux getattr rank` &&
        systemctl list-units --user | grep "flux-sdexec-${rank}-${jobiddec}" &&
        flux cancel ${jobid} &&
        flux job wait-event -v -t 30 $jobid clean &&
        systemctl list-units --user > list-units.out &&
        test_must_fail grep "flux-sdexec-${rank}-${jobiddec}" list-units.out
'
test_expect_success 'job-exec: no cleanup does not cleanup systemd' '
        jobid=$(flux submit \
                --setattr=system.exec.sd.no_cleanup=true \
                hostname) &&
        flux job wait-event -v -t 30 $jobid clean &&
        jobiddec=`flux job id --to=dec $jobid` &&
        rank=`flux getattr rank` &&
        systemctl list-units --user | grep "flux-sdexec-${rank}-${jobiddec}" &&
        systemctl stop --user "flux-sdexec-${rank}-${jobiddec}.service"
'
test_expect_success 'job-exec: shell stdout/err goto eventlog logged by default' '
        cat >testdefaultlog.sh <<-EOF &&
#!/bin/bash
echo "default stdout"
echo "default stderr" 1>&2
EOF
        chmod +x testdefaultlog.sh &&
        jobid=$(flux submit --wait \
                --setattr=system.exec.job_shell="$(pwd)/testdefaultlog.sh" \
                hostname) &&
        flux job eventlog -p guest.exec.eventlog ${jobid} > defaultlog.out &&
        grep "default stdout" defaultlog.out | grep "stream=\"stdout\"" &&
        grep "default stderr" defaultlog.out | grep "stream=\"stderr\""
'
test_expect_success 'job-exec: shell stdout/err goto eventlog when configured' '
        cat >testeventloglog.sh <<-EOF &&
#!/bin/bash
echo "eventlog stdout"
echo "eventlog stderr" 1>&2
EOF
        chmod +x testeventloglog.sh &&
        jobid=$(flux submit --wait \
                --setattr=system.exec.job_shell="$(pwd)/testeventloglog.sh" \
                --setattr=system.exec.sd.stdoutlog="eventlog" \
                --setattr=system.exec.sd.stderrlog="eventlog" \
                hostname) &&
        flux job eventlog -p guest.exec.eventlog ${jobid} > eventloglog.out &&
        grep "eventlog stdout" eventloglog.out | grep "stream=\"stdout\"" &&
        grep "eventlog stderr" eventloglog.out | grep "stream=\"stderr\""
'
# we can't be 100% sure how systemd is setup, so we don't check the
# systemd journal for logging, we just make sure the logging did not
# go to the guest.exec.eventlog
test_expect_success 'job-exec: shell stdout/err dont goto eventlog when configured' '
        cat >testsystemdlog.sh <<-EOF &&
#!/bin/bash
echo "systemd stdout"
echo "systemd stderr" 1>&2
EOF
        chmod +x testsystemdlog.sh &&
        jobid=$(flux submit --wait \
                --setattr=system.exec.job_shell="$(pwd)/testsystemdlog.sh" \
                --setattr=system.exec.sd.stdoutlog="systemd" \
                --setattr=system.exec.sd.stderrlog="systemd" \
                hostname) &&
        flux job eventlog -p guest.exec.eventlog ${jobid} > systemdlog.out &&
        test_must_fail grep "systemd stdout" systemdlog.out &&
        test_must_fail grep "systemd stderr" systemdlog.out
'
test_expect_success 'job-exec: shell stdout/err goto eventlog with bad config' '
        cat >testbadlog.sh <<-EOF &&
#!/bin/bash
echo "bad stdout"
echo "bad stderr" 1>&2
EOF
        chmod +x testbadlog.sh &&
        jobid=$(flux submit --wait \
                --setattr=system.exec.job_shell="$(pwd)/testbadlog.sh" \
                --setattr=system.exec.sd.stdoutlog="bad" \
                --setattr=system.exec.sd.stderrlog="bad" \
                hostname) &&
        flux job eventlog -p guest.exec.eventlog ${jobid} > badlog.out &&
        grep "bad stdout" badlog.out | grep "stream=\"stdout\"" &&
        grep "bad stderr" badlog.out | grep "stream=\"stderr\""
'
# BUS_DEFAULT_TIMEOUT = 25 seconds, to be on safe side we sleep a bit
# above 30 given the test.
# N.B. in newer systemds could speed up test by setting SYSTEMD_BUS_TIMEOUT
test_expect_success LONGTEST 'job-exec: can run job longer than 25 seconds' '
        jobid=$(flux submit sleep 40) &&
        test_expect_code 0 flux job status $jobid
'
test_expect_success LONGTEST 'job-exec: can cancel job after 25 seconds' '
        jobid=$(flux submit sleep 60) &&
        sleep 40 &&
        flux cancel $jobid &&
        test_expect_code 143 flux job status $jobid
'
test_done
