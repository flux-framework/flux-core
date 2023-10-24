#!/bin/sh

test_description='Test flux job info service update lookup/watch'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc
RPC_STREAM=${FLUX_BUILD_DIR}/t/request/rpc_stream
UPDATE_LOOKUP=${FLUX_BUILD_DIR}/t/job-info/update_lookup
UPDATE_WATCH=${FLUX_BUILD_DIR}/t/job-info/update_watch_stream
WAITFILE="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

fj_wait_event() {
	flux job wait-event --timeout=20 "$@"
}

wait_update_watchers() {
	local count=$1
	local i=0
	while (! flux module stats --parse "update_watchers" job-info > /dev/null 2>&1 \
		|| [ "$(flux module stats --parse "update_watchers" job-info 2> /dev/null)" != "${count}" ]) \
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

test_expect_success 'job-info: update lookup with no update events works (job active)' '
	flux submit --wait-event=start sleep 300 > lookup1.id &&
	${UPDATE_LOOKUP} $(cat lookup1.id) R > lookup1.out &&
	flux cancel $(cat lookup1.id) &&
	cat lookup1.out | jq -e ".execution.expiration == 0.0"
'

test_expect_success 'job-info: update lookup with no update events works (job inactive)' '
	flux submit --wait-event=clean hostname > lookup2.id &&
	${UPDATE_LOOKUP} $(cat lookup2.id) R > lookup2.out &&
	cat lookup2.out | jq -e ".execution.expiration == 0.0"
'

test_expect_success 'job-info: update lookup with update events works (job active)' '
	flux submit --wait-event=start sleep 300 > lookup3.id &&
	kvspath=`flux job id --to=kvs $(cat lookup3.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}" &&
	${UPDATE_LOOKUP} $(cat lookup3.id) R > lookup3A.out &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}" &&
	${UPDATE_LOOKUP} $(cat lookup3.id) R > lookup3B.out &&
	flux cancel $(cat lookup3.id) &&
	cat lookup3A.out | jq -e ".execution.expiration == 100.0" &&
	cat lookup3B.out | jq -e ".execution.expiration == 200.0"
'

test_expect_success 'job-info: update lookup with update events works (job inactive)' '
	flux submit --wait-event=start sleep 300 > lookup4.id &&
	kvspath=`flux job id --to=kvs $(cat lookup4.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}" &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}" &&
	flux cancel $(cat lookup4.id) &&
	${UPDATE_LOOKUP} $(cat lookup4.id) R > lookup4.out &&
	cat lookup4.out | jq -e ".execution.expiration == 200.0"
'

test_expect_success 'job-info: update watch with no update events works (job inactive)' '
	flux submit --wait-event=clean hostname > watch1.id &&
	${UPDATE_WATCH} $(cat watch1.id) R > watch1.out &&
	test $(cat watch1.out | wc -l) -eq 1 &&
	cat watch1.out | jq -e ".execution.expiration == 0.0"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with no update events works (job run/canceled)' '
	flux submit --wait-event=start sleep inf > watch2.id
	${UPDATE_WATCH} $(cat watch2.id) R > watch2.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch2.out &&
	flux cancel $(cat watch2.id) &&
	wait $watchpid &&
	test $(cat watch2.out | wc -l) -eq 1 &&
	cat watch2.out | jq -e ".execution.expiration == 0.0"
'

test_expect_success 'job-info: update watch with update events works (job inactive)' '
	flux submit --wait-event=start sleep inf > watch3.id &&
	kvspath=`flux job id --to=kvs $(cat watch3.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}" &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}" &&
	flux cancel $(cat watch3.id) &&
	${UPDATE_WATCH} $(cat watch3.id) R > watch3.out &&
	test $(cat watch3.out | wc -l) -eq 1 &&
	cat watch3.out | jq -e ".execution.expiration == 200.0"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with update events works (before watch)' '
	flux submit --wait-event=start sleep inf > watch4.id &&
	kvspath=`flux job id --to=kvs $(cat watch4.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}"
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}"
	${UPDATE_WATCH} $(cat watch4.id) R > watch4.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch4.out &&
	flux cancel $(cat watch4.id) &&
	wait $watchpid &&
	test $(cat watch4.out | wc -l) -eq 1 &&
	cat watch4.out | jq -e ".execution.expiration == 200.0"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with update events works (after watch)' '
	flux submit --wait-event=start sleep inf > watch5.id
	${UPDATE_WATCH} $(cat watch5.id) R > watch5.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch5.out &&
	kvspath=`flux job id --to=kvs $(cat watch5.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}" &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}" &&
	${WAITFILE} --count=3 --timeout=30 --pattern="expiration" watch5.out &&
	flux cancel $(cat watch5.id) &&
	wait $watchpid &&
	test $(cat watch5.out | wc -l) -eq 3 &&
	head -n1 watch5.out | jq -e ".execution.expiration == 0.0" &&
	head -n2 watch5.out | tail -n1 | jq -e ".execution.expiration == 100.0" &&
	tail -n1 watch5.out | jq -e ".execution.expiration == 200.0"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with update events works (before/after watch)' '
	flux submit --wait-event=start sleep inf > watch6.id &&
	kvspath=`flux job id --to=kvs $(cat watch6.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}"
	${UPDATE_WATCH} $(cat watch6.id) R > watch6.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch6.out &&
	kvspath=`flux job id --to=kvs $(cat watch6.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}" &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch6.out &&
	flux cancel $(cat watch6.id) &&
	wait $watchpid &&
	test $(cat watch6.out | wc -l) -eq 2 &&
	head -n1 watch6.out | jq -e ".execution.expiration == 100.0" &&
	tail -n1 watch6.out | jq -e ".execution.expiration == 200.0"
'

# signaling the update watch tool with SIGUSR1 will cancel the stream
test_expect_success NO_CHAIN_LINT 'job-info: update watch can be canceled (single watcher)' '
	flux submit --wait-event=start sleep inf > watch7.id
	${UPDATE_WATCH} $(cat watch7.id) R > watch7.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch7.out &&
	kill -s USR1 $watchpid &&
	wait $watchpid &&
	flux cancel $(cat watch7.id) &&
	test $(cat watch7.out | wc -l) -eq 1 &&
	cat watch7.out | jq -e ".execution.expiration == 0.0"
'

# This test may look tricky.  Basically we are updating the eventlog
# with resource-update events once in awhile and starting watchers at
# different point in times in the eventlog's parsing.
#
# At the end, the first watcher should see 3 R versions, the second
# one 2, and the last one only 1.
test_expect_success NO_CHAIN_LINT 'job-info: update watch with multiple watchers works' '
	flux submit --wait-event=start sleep inf > watch8.id
	${UPDATE_WATCH} $(cat watch8.id) R > watch8A.out &
	watchpidA=$! &&
	wait_update_watchers 1 &&
	kvspath=`flux job id --to=kvs $(cat watch8.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}" &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch8A.out
	${UPDATE_WATCH} $(cat watch8.id) R > watch8B.out &
	watchpidB=$! &&
	wait_update_watchers 2 &&
	kvspath=`flux job id --to=kvs $(cat watch8.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 200.0}" &&
	${WAITFILE} --count=3 --timeout=30 --pattern="expiration" watch8A.out &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch8B.out
	${UPDATE_WATCH} $(cat watch8.id) R > watch8C.out &
	watchpidC=$! &&
	wait_update_watchers 3 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch8C.out &&
	flux cancel $(cat watch8.id) &&
	wait $watchpidA &&
	wait $watchpidB &&
	wait $watchpidC &&
	test $(cat watch8A.out | wc -l) -eq 3 &&
	test $(cat watch8B.out | wc -l) -eq 2 &&
	test $(cat watch8C.out | wc -l) -eq 1 &&
	head -n1 watch8A.out | jq -e ".execution.expiration == 0.0" &&
	head -n2 watch8A.out | tail -n1 | jq -e ".execution.expiration == 100.0" &&
	tail -n1 watch8A.out | jq -e ".execution.expiration == 200.0" &&
	head -n1 watch8B.out | jq -e ".execution.expiration == 100.0" &&
	tail -n1 watch8B.out | jq -e ".execution.expiration == 200.0" &&
	tail -n1 watch8C.out | jq -e ".execution.expiration == 200.0"
'

# signaling the update watch tool with SIGUSR1 will cancel the stream
test_expect_success NO_CHAIN_LINT 'job-info: update watch can be canceled (multiple watchers)' '
	flux submit --wait-event=start sleep inf > watch10.id
	${UPDATE_WATCH} $(cat watch10.id) R > watch10A.out &
	watchpidA=$! &&
	wait_update_watchers 1 &&
	kvspath=`flux job id --to=kvs $(cat watch10.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}"
	${UPDATE_WATCH} $(cat watch10.id) R > watch10B.out &
	watchpidB=$! &&
	wait_update_watchers 2 &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch10A.out &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch10B.out &&
	kill -s USR1 $watchpidA &&
	wait $watchpidA &&
	kill -s USR1 $watchpidB &&
	wait $watchpidB &&
	flux cancel $(cat watch10.id) &&
	test $(cat watch10A.out | wc -l) -eq 2 &&
	test $(cat watch10B.out | wc -l) -eq 1 &&
	head -n1 watch10A.out | jq -e ".execution.expiration == 0.0" &&
	tail -n1 watch10B.out | jq -e ".execution.expiration == 100.0" &&
	cat watch10B.out | jq -e ".execution.expiration == 100.0"
'

# If someone is already doing an update-watch on a jobid/key, update-lookup can
# return the cached info
test_expect_success NO_CHAIN_LINT 'job-info: update lookup returns cached R from update watch' '
	flux submit --wait-event=start sleep inf > watch9.id
	${UPDATE_WATCH} $(cat watch9.id) R > watch9.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	kvspath=`flux job id --to=kvs $(cat watch9.id)` &&
	flux kvs eventlog append ${kvspath}.eventlog resource-update "{\"expiration\": 100.0}" &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch9.out &&
        ${UPDATE_LOOKUP} $(cat watch9.id) R > lookup9.out &&
	flux cancel $(cat watch9.id) &&
	wait $watchpid &&
	test $(cat watch9.out | wc -l) -eq 2 &&
	head -n1 watch9.out | jq -e ".execution.expiration == 0.0" &&
	tail -n1 watch9.out | jq -e ".execution.expiration == 100.0" &&
	cat lookup9.out | jq -e ".execution.expiration == 100.0"
'

#
# security tests
#

set_userid() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
	unset FLUX_HANDLE_USERID
	unset FLUX_HANDLE_ROLEMASK
}

test_expect_success 'job-info: non job owner cannot lookup key' '
	jobid=`flux submit --wait-event=start sleep inf` &&
	set_userid 9999 &&
	test_must_fail ${UPDATE_LOOKUP} $jobid R &&
	unset_userid &&
	flux cancel $jobid
'

test_expect_success 'job-info: non job owner cannot watch key' '
	jobid=`flux submit --wait-event=start sleep inf` &&
	set_userid 9999 &&
	test_must_fail ${UPDATE_WATCH} $jobid R &&
	unset_userid &&
	flux cancel $jobid
'

# this test checks security on a second watcher, which is trying to
# access cached info
test_expect_success NO_CHAIN_LINT 'job-info: non job owner cannot watch key (second watcher)' '
	jobid=`flux submit --wait-event=start sleep inf`
	${UPDATE_WATCH} $jobid R > watchsecurity.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watchsecurity.out &&
	set_userid 9999 &&
	test_must_fail ${UPDATE_WATCH} $jobid R &&
	unset_userid &&
	flux cancel $jobid &&
	wait $watchpid
'

# update-lookup cannot read cached watch data
test_expect_success NO_CHAIN_LINT 'job-info: non job owner cannot lookup key (piggy backed)' '
	jobid=`flux submit --wait-event=start sleep inf`
	${UPDATE_WATCH} $jobid R > lookupsecurity.out &
	watchpid=$! &&
	wait_update_watchers 1 &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" lookupsecurity.out &&
	set_userid 9999 &&
	test_must_fail ${UPDATE_LOOKUP} $jobid R &&
	unset_userid &&
	flux cancel $jobid &&
	wait $watchpid
'

#
# stats & corner cases
#

test_expect_success 'job-info stats works' '
	flux module stats --parse update_lookups job-info &&
	flux module stats --parse update_watchers job-info
'
test_expect_success 'update-lookup request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.update-watch 71 </dev/null
'
test_expect_success 'update-watch request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.update-watch 71 </dev/null
'
test_expect_success 'update-lookup request with invalid key fails with EINVAL(22))' '
	echo "{\"id\":42, \"key\":\"foobar\", \"flags\":0}" \
		| ${RPC} job-info.update-lookup 22
'
test_expect_success 'update-watch request invalid key fails' '
	echo "{\"id\":42, \"key\":\"foobar\", \"flags\":0}" \
		| test_must_fail ${RPC_STREAM} job-info.update-watch 2> invalidkey.err && \
	grep "unsupported key" invalidkey.err
'
test_expect_success 'update-lookup request with invalid flags fails with EPROTO(71))' '
	echo "{\"id\":42, \"key\":\"R\", \"flags\":499}" \
		| ${RPC} job-info.update-lookup 71
'
test_expect_success 'update-watch request invalid flags fails' '
	echo "{\"id\":42, \"key\":\"R\", \"flags\":499}" \
		| test_must_fail ${RPC_STREAM} job-info.update-watch 2> invalidflags.err &&
	grep "invalid flag" invalidflags.err
'
test_expect_success 'update-watch request non-streaming fails with EPROTO(71)' '
	echo "{\"id\":42, \"key\":\"R\", \"flags\":0}" \
		| ${RPC} job-info.update-watch 71
'

test_done
