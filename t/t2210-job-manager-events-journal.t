#!/bin/sh

test_description='Test flux job manager journal service'

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)

test_under_flux 4 full -Slog-stderr-level=1

RPC=${FLUX_BUILD_DIR}/t/request/rpc
EVENTS_JOURNAL_STREAM=${FLUX_BUILD_DIR}/t/job-manager/events_journal_stream

test_expect_success 'flux-job: generate jobspec for simple test job' '
	flux run --dry-run -n1 hostname >basic.json
'

check_event() {
	jobid="$1"
	key=$2
	value=$3
	filename=$4
	if cat $filename \
		| jq -e ".id == ${jobid} and ${key} == ${value}" \
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
			| jq -e ".id == ${jobid} and ${key} == ${value}" \
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

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal w/ no filters shows all events' '
	jq -j -c -n "{}" \
		| $EVENTS_JOURNAL_STREAM > events1.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal allow works' '
	jq -j -c -n "{allow:{clean:1}}" \
		| $EVENTS_JOURNAL_STREAM > events2.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal allow works (multiple)' '
	jq -j -c -n "{allow:{depend:1, finish:1, clean:1}}" \
		| $EVENTS_JOURNAL_STREAM > events3.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal deny works' '
	jq -j -c -n "{deny:{finish:1}}" \
		| $EVENTS_JOURNAL_STREAM > events4.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal deny works (multiple)' '
	jq -j -c -n "{deny:{depend:1, finish:1, release:1}}" \
		| $EVENTS_JOURNAL_STREAM > events5.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal allow & deny works' '
	jq -j -c -n "{allow:{depend:1, finish:1, clean:1}, deny:{depend:1}}" \
		| $EVENTS_JOURNAL_STREAM > events6.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-manager: events-journal contains older jobs' '
	jobid1=`flux job submit basic.json | flux job id`
	jobid2=`flux job submit basic.json | flux job id`
	flux job wait-event ${jobid1} clean
	flux job wait-event ${jobid2} clean
	jq -j -c -n "{full:true, allow:{depend:1, clean:1}}" \
		| $EVENTS_JOURNAL_STREAM > events7.out &
	pid=$! &&
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
	wait $pid
'

test_expect_success 'job-manager: events-journal request fails with EPROTO on empty payload' '
	$RPC job-manager.events-journal 71 < /dev/null
'

test_expect_success 'job-manager: events-journal request fails if not streaming RPC' '
	jq -j -c -n "{}" > cc1.in &&
	$RPC job-manager.events-journal 71 < cc1.in
'

test_expect_success 'job-manager: events-journal request fails if allow not an object' '
	jq -j -c -n "{allow:5}" > cc2.in &&
	test_must_fail $EVENTS_JOURNAL_STREAM < cc2.in 2> cc2.err &&
	grep "allow should be an object" cc2.err
'

test_expect_success 'job-manager: events-journal request fails if deny not an object' '
	jq -j -c -n "{deny:5}" > cc3.in &&
	test_must_fail $EVENTS_JOURNAL_STREAM < cc3.in 2> cc3.err &&
	grep "deny should be an object" cc3.err
'

test_done
