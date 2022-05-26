#
#  Ensure jobs and scheduling continue to work if instance is restarted
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
test_expect_success 'job-exec: submit two jobs that consumes all resources' '
        NCORES=$(flux resource list -s up -no "{ncores}") &&
        flux mini submit -n ${NCORES} sleep 1000 > jobid1 &&
        flux job wait-event $(cat jobid1) start &&
        flux mini submit -n ${NCORES} sleep 1000 > jobid2
'
test_expect_success 'job-exec: verify jobs listed and in expected state' '
        flux jobs --filter=running | grep $(cat jobid1) &&
        flux jobs --filter=pending | grep $(cat jobid2)
'
test_expect_success 'job-exec: restart flux' '
        sudo systemctl restart flux
'
test_expect_success 'job-exec: wait for flux to finish setting up' '
        until flux jobs | grep $(cat jobid1) 2>/dev/null; do
              sleep 1
        done
'
test_expect_success 'job-exec: verify jobs still listed and in expected state' '
        flux jobs --filter=running | grep $(cat jobid1) &&
        flux jobs --filter=pending | grep $(cat jobid2)
'
test_expect_success 'job-exec: cancel job1 and make sure job2 will run' '
        flux job cancel $(cat jobid1) &&
        flux job wait-event -t 60 $(cat jobid1) clean &&
        flux job wait-event -t 60 $(cat jobid2) start
'
test_expect_success 'job-exec: verify jobs listed and in new expected state' '
        flux jobs --filter=canceled | grep $(cat jobid1) &&
        flux jobs --filter=running | grep $(cat jobid2)
'
test_expect_success 'job-exec: cancel jobs' '
        flux job cancel $(cat jobid2)
'
