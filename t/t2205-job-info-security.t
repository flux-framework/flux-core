#!/bin/sh

test_description='Test flux job info service security'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

# we have to fake a job submission by a guest into the KVS.

# it is not 100% clear the most portable way to add a newline to a
# string, so I do a classic trick of adding an extra char at the end
# to avoid newline stripping, then strip the extra char.

submit_job() {
        userid=$1
        jobid=$(flux job submit test.json)
        eventlog=$(flux job eventlog --context-format=json $jobid)
        eventlogsub=$(echo $eventlog | sed -e "s/\(\"userid\":\)[0-9]*/\1${userid}/")
        eventlognew=$(printf "${eventlogsub}"'\n'"X")
        eventlognew=${eventlognew%?}
        kvsdir=$(flux job id --to=kvs-active $jobid)
        flux kvs put ${kvsdir}.eventlog="${eventlognew}"
        echo $jobid
}

bad_first_event() {
        userid=$1
        jobid=$(flux job submit test.json)
        eventlog=$(flux job eventlog --context-format=json $jobid)
        eventlogsub=$(echo $eventlog | sed -e "s/submit/foobar/")
        eventlognew=$(printf "${eventlogsub}"'\n'"X")
        eventlognew=${eventlognew%?}
        kvsdir=$(flux job id --to=kvs-active $jobid)
        flux kvs put ${kvsdir}.eventlog="${eventlognew}"
        echo $jobid
}

set_userid() {
        export FLUX_HANDLE_USERID=$1
        export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
        unset FLUX_HANDLE_USERID
        unset FLUX_HANDLE_ROLEMASK
}

test_expect_success 'job-info: generate jobspec for simple test job' '
        flux jobspec --format json srun -N1 hostname > test.json
'

test_expect_success 'flux job eventlog works (owner)' '
        jobid=$(submit_job $(id -u)) &&
        flux job eventlog $jobid
'

test_expect_success 'flux job eventlog works (user)' '
        jobid=$(submit_job 9000) &&
        set_userid 9000 &&
        flux job eventlog $jobid &&
        unset_userid
'

test_expect_success 'flux job eventlog fails (wrong user)' '
        jobid=$(submit_job 9000) &&
        set_userid 9999 &&
        ! flux job eventlog $jobid &&
        unset_userid
'

test_expect_success 'flux job eventlog fails on bad first event (user)' '
        jobid=$(bad_first_event 9000) &&
        set_userid 9999 &&
        ! flux job eventlog $jobid &&
        unset_userid
'

test_done
