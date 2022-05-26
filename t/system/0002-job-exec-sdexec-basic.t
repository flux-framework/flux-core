#
#  Basic job-exec sdexec / systemd tests
#

test_expect_success 'job-exec: setup system instance to use systemd' '
        method=$(sudo -u flux flux config get --default=notset exec.method)
        if test "${method}" != "systemd"
        then
                if ! sudo test -f /etc/flux/system/conf.d/exec.toml
                then
                        sudo touch /etc/flux/system/conf.d/exec.toml
                        sudo bash -c "echo [exec] >> /etc/flux/system/conf.d/exec.toml"
                fi
                sudo bash -c "echo method = \\\"systemd\\\" >> /etc/flux/system/conf.d/exec.toml"
                sudo systemctl restart flux
                until flux mini run hostname 2>/dev/null; do
                        sleep 1
                done
        fi
'
test_expect_success 'job-exec: verify system instance using systemd' '
        jobid=$(flux mini submit sleep 100) &&
        flux job wait-event -v -t 60 $jobid start &&
        jobiddec=`flux job id --to=dec $jobid` &&
        rank=`flux getattr rank` &&
        sudo -u flux systemctl list-units --user | grep "flux-sdexec-${rank}-${jobiddec}" &&
        flux job cancel ${jobid}
'
test_expect_success 'job-exec: simple job exits 0 on success' '
        jobid=$(flux mini submit /bin/true) &&
        test_expect_code 0 flux job status $jobid
'
test_expect_success 'job-exec: simple job exits 1 on failure' '
        jobid=$(flux mini submit /bin/false) &&
        test_expect_code 1 flux job status -vv $jobid
'
test_expect_success 'job-exec: simple job exits 127 on bad command' '
        jobid=$(flux mini submit /bin/foobar) &&
        test_expect_code 127 flux job status $jobid
'
# When sending a signal very shortly after a job is started, there is
# a small race where we don't know where the signal is actually sent.
# It could be sent to the imp or job shell before actual job tasks are
# started.  To ensure consistent test results, wait for shell.start in
# guest.exec.eventlog.
test_expect_success 'job-exec: simple job exits 138 on signaled job' '
        jobid=$(flux mini submit sleep 30) &&
        flux job wait-event -t 10 ${jobid} start &&
        flux job wait-event -p guest.exec.eventlog -t 10 ${jobid} shell.start &&
        flux job kill --signal=SIGUSR1 $jobid &&
        test_expect_code 138 flux job status $jobid
'
# Similar to above test, wait for shell.start in guest.exec.eventlog
# to ensure consistent test results.  The job cancel will send a
# SIGTERM to the shell/tasks.
test_expect_success 'job-exec: simple job can be canceled' '
        jobid=$(flux mini submit sleep 30) &&
        flux job wait-event -t 10 ${jobid} start &&
        flux job wait-event -p guest.exec.eventlog -t 10 ${jobid} shell.start &&
        flux job cancel $jobid &&
        flux job wait-event -t 10 ${jobid} clean &&
        test_expect_code 143 flux job status $jobid
'
test_expect_success 'job-exec: job fails correctly if user service not setup' '
        jobid=$(flux mini submit \
                --setattr=system.exec.sd.test_exec_fail=true \
                hostname) &&
        flux job wait-event -v -t 60 $jobid clean &&
        flux job eventlog ${jobid} | grep "test sdprocess_exec" \
             | grep "Operation not permitted"
'
