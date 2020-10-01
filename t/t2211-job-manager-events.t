#!/bin/sh

test_description='Test flux job manager events service'

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)

# set events_cache_maxlen to something more sensible in testing,
# otherwise we'll be parsing 100s of entries regularly.  20 is a good
# number, since it will always cover the prior two jobs that were
# executed.
cat >job-manager.toml <<EOF
[job-manager]
events_cache_maxlen = 20
EOF

test_under_flux 4

RPC=${FLUX_BUILD_DIR}/t/request/rpc
EVENT_STREAM=${FLUX_BUILD_DIR}/t/job-manager/event_stream

flux setattr log-stderr-level 1

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

wait_events_listeners() {
        num=$1
        i=0
        while [ "$(flux module stats --parse events.listeners job-manager 2> /dev/null)" != "$num" ] \
              && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

# to avoid raciness in tests below, ensure job-info is synced to the
# job-manager and won't be part of the events.listeners counts below
test_expect_success 'wait for job-info to sync with job-manager' '
        wait_events_listeners 1
'

check_event() {
        jobid="$1"
        key=$2
        value=$3
        filename=$4
        if cat $filename \
            | $jq -e ".id == ${jobid} and ${key} == ${value}" \
            | grep -q "true"
        then
            return 0
        fi
        return 1
}

check_event_name() {
        jobid="$1"
        name=$2
        filename=$3
        check_event ${jobid} .entry.name \"${name}\" ${filename}
}

check_event_annotation() {
        jobid="$1"
        key=$2
        value=$3
        filename=$4
        check_event ${jobid} .entry.context.annotations.user.${key} ${value} ${filename}
}

wait_event() {
        jobid="$1"
        key=$2
        value=$3
        filename=$4
        i=0 &&
        while [ $i -lt 50 ]
        do
            if cat $filename \
                | $jq -e ".id == ${jobid} and ${key} == ${value}" \
                | grep -q "true"
            then
                return 0
            fi
            sleep 0.1
            i=$((i + 1))
        done
        return 1
}

wait_event_name() {
        jobid="$1"
        name=$2
        filename=$3
        wait_event ${jobid} .entry.name \"${name}\" ${filename}
}

wait_event_annotation() {
        jobid="$1"
        key=$2
        value=$3
        filename=$4
        wait_event ${jobid} .entry.context.annotations.user.${key} ${value} ${filename}
}

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events with no filters shows all events' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{}" \
          | $EVENT_STREAM > events1.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid} clean events1.out &&
        check_event_name ${jobid} submit events1.out &&
        check_event_name ${jobid} depend events1.out &&
        check_event_name ${jobid} alloc events1.out &&
        check_event_name ${jobid} start events1.out &&
        check_event_name ${jobid} finish events1.out &&
        check_event_name ${jobid} release events1.out &&
        check_event_name ${jobid} free events1.out &&
        check_event_name ${jobid} clean events1.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events with allow works' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{allow:{clean:1}}" \
          | $EVENT_STREAM > events2.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid} clean events2.out &&
        test_must_fail check_event_name ${jobid} submit events2.out &&
        test_must_fail check_event_name ${jobid} depend events2.out &&
        test_must_fail check_event_name ${jobid} alloc events2.out &&
        test_must_fail check_event_name ${jobid} start events2.out &&
        test_must_fail check_event_name ${jobid} finish events2.out &&
        test_must_fail check_event_name ${jobid} release events2.out &&
        test_must_fail check_event_name ${jobid} free events2.out &&
        check_event_name ${jobid} clean events2.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events with allow works (multiple)' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{allow:{depend:1, finish:1, clean:1}}" \
          | $EVENT_STREAM > events3.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid} clean events3.out &&
        test_must_fail check_event_name ${jobid} submit events3.out &&
        check_event_name ${jobid} depend events3.out &&
        test_must_fail check_event_name ${jobid} alloc events3.out &&
        test_must_fail check_event_name ${jobid} start events3.out &&
        check_event_name ${jobid} finish events3.out &&
        test_must_fail check_event_name ${jobid} release events3.out &&
        test_must_fail check_event_name ${jobid} free events3.out &&
        check_event_name ${jobid} clean events3.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events with deny works' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{deny:{finish:1}}" \
          | $EVENT_STREAM > events4.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid} clean events4.out &&
        check_event_name ${jobid} submit events4.out &&
        check_event_name ${jobid} depend events4.out &&
        check_event_name ${jobid} alloc events4.out &&
        check_event_name ${jobid} start events4.out &&
        test_must_fail check_event_name ${jobid} finish events4.out &&
        check_event_name ${jobid} release events4.out &&
        check_event_name ${jobid} free events4.out &&
        check_event_name ${jobid} clean events4.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events with deny works (multiple)' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{deny:{depend:1, finish:1, release:1}}" \
          | $EVENT_STREAM > events5.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid} clean events5.out &&
        check_event_name ${jobid} submit events5.out &&
        test_must_fail check_event_name ${jobid} depend events5.out &&
        check_event_name ${jobid} alloc events5.out &&
        check_event_name ${jobid} start events5.out &&
        test_must_fail check_event_name ${jobid} finish events5.out &&
        test_must_fail check_event_name ${jobid} release events5.out &&
        check_event_name ${jobid} free events5.out &&
        check_event_name ${jobid} clean events5.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events with allow & deny works' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{allow:{depend:1, finish:1, clean:1}, deny:{depend:1}}" \
          | $EVENT_STREAM > events6.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid} clean events6.out &&
        test_must_fail check_event_name ${jobid} submit events6.out &&
        test_must_fail check_event_name ${jobid} depend events6.out &&
        test_must_fail check_event_name ${jobid} alloc events6.out &&
        test_must_fail check_event_name ${jobid} start events6.out &&
        check_event_name ${jobid} finish events6.out &&
        test_must_fail check_event_name ${jobid} release events6.out &&
        test_must_fail check_event_name ${jobid} free events6.out &&
        check_event_name ${jobid} clean events6.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events journaling works' '
        jobid1=`flux job submit basic.json | flux job id`
        jobid2=`flux job submit basic.json | flux job id`
        flux job wait-event ${jobid1} clean
        flux job wait-event ${jobid2} clean
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{allow:{depend:1, clean:1}}" \
          | $EVENT_STREAM > events7.out &
        pid=$! &&
        wait_events_listeners $count &&
        jobid3=`flux job submit basic.json | flux job id` &&
        wait_event_name ${jobid3} clean events7.out &&
        test_must_fail check_event_name ${jobid1} submit events7.out &&
        check_event_name ${jobid1} depend events7.out &&
        test_must_fail check_event_name ${jobid1} alloc events7.out &&
        test_must_fail check_event_name ${jobid1} start events7.out &&
        test_must_fail check_event_name ${jobid1} finish events7.out &&
        test_must_fail check_event_name ${jobid1} release events7.out &&
        test_must_fail check_event_name ${jobid1} free events7.out &&
        check_event_name ${jobid1} clean events7.out &&
        test_must_fail check_event_name ${jobid2} submit events7.out &&
        check_event_name ${jobid2} depend events7.out &&
        test_must_fail check_event_name ${jobid2} alloc events7.out &&
        test_must_fail check_event_name ${jobid2} start events7.out &&
        test_must_fail check_event_name ${jobid2} finish events7.out &&
        test_must_fail check_event_name ${jobid2} release events7.out &&
        test_must_fail check_event_name ${jobid2} free events7.out &&
        check_event_name ${jobid2} clean events7.out &&
        test_must_fail check_event_name ${jobid3} submit events7.out &&
        check_event_name ${jobid3} depend events7.out &&
        test_must_fail check_event_name ${jobid3} alloc events7.out &&
        test_must_fail check_event_name ${jobid3} start events7.out &&
        test_must_fail check_event_name ${jobid3} finish events7.out &&
        test_must_fail check_event_name ${jobid3} release events7.out &&
        test_must_fail check_event_name ${jobid3} free events7.out &&
        check_event_name ${jobid3} clean events7.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events works with annotations' '
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{allow:{annotations:1, clean:1}}" \
          | $EVENT_STREAM > events8.out &
        pid=$! &&
        wait_events_listeners $count &&
        flux queue stop &&
        jobid=`flux job submit basic.json | flux job id` &&
        echo ${jobid} > annotation_job1.id &&
        flux job annotate $jobid foo abcdefg &&
        wait_event_annotation ${jobid} foo \"abcdefg\" events8.out &&
        flux job annotate $jobid foo ABCDEFG &&
        wait_event_annotation ${jobid} foo \"ABCDEFG\" events8.out &&
        flux job annotate $jobid foo 1234567 &&
        wait_event_annotation ${jobid} foo 1234567 events8.out &&
        flux queue start &&
        wait_event_name ${jobid} clean events8.out &&
        test_must_fail check_event_name ${jobid} submit events8.out &&
        test_must_fail check_event_name ${jobid} depend events8.out &&
        test_must_fail check_event_name ${jobid} alloc events8.out &&
        test_must_fail check_event_name ${jobid} start events8.out &&
        test_must_fail check_event_name ${jobid} finish events8.out &&
        test_must_fail check_event_name ${jobid} release events8.out &&
        test_must_fail check_event_name ${jobid} free events8.out &&
        check_event_annotation ${jobid} foo \"abcdefg\" events8.out &&
        check_event_annotation ${jobid} foo \"ABCDEFG\" events8.out &&
        check_event_annotation ${jobid} foo 1234567 events8.out &&
        check_event_name ${jobid} clean events8.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success HAVE_JQ,NO_CHAIN_LINT 'job-manager: events journaling works with annotations' '
        jobid1=`cat annotation_job1.id`
        before=`flux module stats --parse events.listeners job-manager`
        count=$((before + 1))
        $jq -j -c -n "{allow:{annotations:1, clean:1}}" \
          | $EVENT_STREAM > events9.out &
        pid=$! &&
        wait_events_listeners $count &&
        flux queue stop &&
        jobid2=`flux job submit basic.json | flux job id` &&
        flux job annotate ${jobid2} bar hijklmnop &&
        wait_event_annotation ${jobid2} bar \"hijklmnop\" events9.out &&
        flux job annotate $jobid2 bar HIJKLMNOP &&
        wait_event_annotation ${jobid2} bar \"HIJKLMNOP\" events9.out &&
        flux job annotate $jobid2 bar 89012345 &&
        wait_event_annotation ${jobid2} bar 89012345 events9.out &&
        flux queue start &&
        wait_event_name ${jobid2} clean events9.out &&
        test_must_fail check_event_name ${jobid1} submit events9.out &&
        test_must_fail check_event_name ${jobid1} depend events9.out &&
        test_must_fail check_event_name ${jobid1} alloc events9.out &&
        test_must_fail check_event_name ${jobid1} start events9.out &&
        test_must_fail check_event_name ${jobid1} finish events9.out &&
        test_must_fail check_event_name ${jobid1} release events9.out &&
        test_must_fail check_event_name ${jobid1} free events9.out &&
        check_event_annotation ${jobid1} foo \"abcdefg\" events9.out &&
        check_event_annotation ${jobid1} foo \"ABCDEFG\" events9.out &&
        check_event_annotation ${jobid1} foo 1234567 events9.out &&
        check_event_name ${jobid1} clean events9.out &&
        test_must_fail check_event_name ${jobid2} submit events9.out &&
        test_must_fail check_event_name ${jobid2} depend events9.out &&
        test_must_fail check_event_name ${jobid2} alloc events9.out &&
        test_must_fail check_event_name ${jobid2} start events9.out &&
        test_must_fail check_event_name ${jobid2} finish events9.out &&
        test_must_fail check_event_name ${jobid2} release events9.out &&
        test_must_fail check_event_name ${jobid2} free events9.out &&
        check_event_annotation ${jobid2} bar \"hijklmnop\" events9.out &&
        check_event_annotation ${jobid2} bar \"HIJKLMNOP\" events9.out &&
        check_event_annotation ${jobid2} bar 89012345 events9.out &&
        check_event_name ${jobid2} clean events9.out &&
        kill -s USR1 $pid &&
        wait $pid &&
        wait_events_listeners $before
'

test_expect_success 'job-manager: events request fails with EPROTO on empty payload' '
        $RPC job-manager.events 71 < /dev/null
'

test_expect_success HAVE_JQ 'job-manager: events request fails if not streaming RPC' '
        $jq -j -c -n "{}" > cc1.in &&
        test_must_fail $RPC job-manager.events < cc1.in
'

test_expect_success HAVE_JQ 'job-manager: events request fails if allow not an object' '
        $jq -j -c -n "{allow:5}" > cc2.in &&
        test_must_fail $EVENT_STREAM < cc2.in 2> cc2.err &&
        grep "allow should be an object" cc2.err
'

test_expect_success HAVE_JQ 'job-manager: events request fails if deny not an object' '
        $jq -j -c -n "{deny:5}" > cc3.in &&
        test_must_fail $EVENT_STREAM < cc3.in 2> cc3.err &&
        grep "deny should be an object" cc3.err
'

test_done
