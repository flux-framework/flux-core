#!/bin/bash -e

# test-prereqs: HAVE_JQ

EVENTS_JOURNAL_STREAM=${FLUX_BUILD_DIR}/t/job-manager/events_journal_stream

# wait for jobid in events file to avoid race
# arg1 - file
# arg2 - jobid
wait_jobid_event() {
    local file=$1
    local jobid=$2
    for i in `seq 1 30`
    do
        if grep -q ${jobid} ${file}
        then
            break
        fi
        sleep 1
    done
    if [ "${i}" -eq "30" ]
    then
        return 1
    fi
    return 0
}

jobid1=`flux submit --wait hostname`
jobid2=`flux submit --wait hostname`

jq -j -c -n "{}" | $EVENTS_JOURNAL_STREAM > events1.out &
pid1=$!

jobid1dec=`flux job id --to=dec ${jobid1}`
jobid2dec=`flux job id --to=dec ${jobid2}`

wait_jobid_event events1.out ${jobid2dec}

# kill background process
kill -s SIGUSR1 ${pid1}

# jobid1 completed first, so if jobid2 is in events journal, jobid1
# should be too

grep ${jobid1dec} events1.out

flux job purge --force --num-limit=1

jq -j -c -n "{}" | $EVENTS_JOURNAL_STREAM > events2.out &
pid2=$!

wait_jobid_event events2.out ${jobid2dec}

# kill background process
kill -s SIGUSR1 ${pid2}

# jobid1 should be purged now and not in the events journal

! grep ${jobid1dec} events2.out
