#!/bin/sh

test_description='Test flux job info service lookup w/ update and update-watch'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc
RPC_STREAM=${FLUX_BUILD_DIR}/t/request/rpc_stream
INFO_LOOKUP=${FLUX_BUILD_DIR}/t/job-info/info_lookup
UPDATE_LOOKUP=${FLUX_BUILD_DIR}/t/job-info/update_lookup
UPDATE_WATCH=${FLUX_BUILD_DIR}/t/job-info/update_watch_stream
WAITFILE="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

fj_wait_event() {
	flux job wait-event --timeout=20 "$@"
}

get_update_watchers() {
	flux module stats --parse "update_watchers" job-info
}

wait_update_watchers() {
	local count=$1
	local i=0
	echo "waiting for $count watchers"
	while (! flux module stats --parse "update_watchers" job-info \
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

# Usage: expiration_add JOBID N
# Set expiration of job with JOBID to now + N seconds
#
expiration_add() {
	local id=$1
	local amount=$2
	local kvsdir=$(flux job id --to=kvs $id)
	local new=$(($(date +%s) + $amount))
	flux kvs eventlog append \
	    ${kvsdir}.eventlog resource-update "{\"expiration\": $new}"
	echo $new
}

# Usage: check_expiration jobid VALUE
# Check and wait for expiration to reach VALUE.
#
check_expiration() {
	local jobid=$1
	local value=$2
	local i=0
	while (! ${INFO_LOOKUP} -c ${jobid} R | jq -e ".execution.expiration == ${value}" \
		&& [ $i -lt 200 ] )
	do
		sleep 0.1
		i=$((i + 1))
	done
	if [ "$i" -eq "200" ]
	then
		return 1
	fi
	return 0
}

# Usage: check_expiration_legacy jobid VALUE
# Check and wait for expiration to reach VALUE.
#
check_expiration_legacy() {
	local jobid=$1
	local value=$2
	local i=0
	while (! ${UPDATE_LOOKUP} ${jobid} R | jq -e ".execution.expiration == ${value}" \
		&& [ $i -lt 200 ] )
	do
		sleep 0.1
		i=$((i + 1))
	done
	if [ "$i" -eq "200" ]
	then
		return 1
	fi
	return 0
}

test_expect_success 'job-info: lookup current with no update events works (job active)' '
	jobid=$(flux submit --wait-event=start sleep 300) &&
	check_expiration $jobid 0.0 &&
	check_expiration_legacy $jobid 0.0 &&
	flux cancel $jobid
'

test_expect_success 'job-info: lookup current with no update events works (job inactive)' '
	jobid=$(flux submit --wait-event=clean hostname) &&
	check_expiration $jobid 0.0 &&
	check_expiration_legacy $jobid 0.0
'

test_expect_success 'job-info: lookup current with update events works (job active)' '
	jobid=$(flux submit --wait-event=start sleep 300) &&
	update1=$(expiration_add $jobid 100) &&
	flux job eventlog $jobid &&
	check_expiration $jobid ${update1} &&
	check_expiration_legacy $jobid ${update1} &&
	update2=$(expiration_add $jobid 200) &&
	check_expiration $jobid ${update2} &&
	check_expiration_legacy $jobid ${update2} &&
	flux cancel $jobid
'

test_expect_success 'job-info: lookup current with update events works (job inactive)' '
	jobid=$(flux submit --wait-event=start sleep 300) &&
	update1=$(expiration_add $jobid 100) &&
	update2=$(expiration_add $jobid 100) &&
	flux cancel $jobid &&
	check_expiration $jobid ${update2} &&
	check_expiration_legacy $jobid ${update2}
'

test_expect_success 'job-info: update watch with no update events works (job inactive)' '
	jobid=$(flux submit --wait-event=clean hostname) &&
	${UPDATE_WATCH} $jobid R > watch1.out &&
	test $(cat watch1.out | wc -l) -eq 1 &&
	cat watch1.out | jq -e ".execution.expiration == 0.0"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with no update events works (job run/canceled)' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf)
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch2.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} -v --count=1 --timeout=30 --pattern="expiration" watch2.out &&
	flux cancel $jobid &&
	wait $watchpid &&
	test_debug "cat watch2.out" &&
	test $(cat watch2.out | wc -l) -eq 1 &&
	cat watch2.out | jq -e ".execution.expiration == 0.0"
'

test_expect_success 'job-info: update watch with update events works (job inactive)' '
	jobid=$(flux submit --wait-event=start sleep inf) &&
	update1=$(expiration_add $jobid 100) &&
	update2=$(expiration_add $jobid 200) &&
	flux cancel $jobid &&
	${UPDATE_WATCH} $jobid R > watch3.out &&
	test $(cat watch3.out | wc -l) -eq 1 &&
	cat watch3.out | jq -e ".execution.expiration == ${update2}"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with update events works (before watch)' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf) &&
	update1=$(expiration_add $jobid 100) &&
	update2=$(expiration_add $jobid 200) &&
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch4.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch4.out &&
	flux cancel $jobid &&
	wait $watchpid &&
	test $(cat watch4.out | wc -l) -eq 1 &&
	cat watch4.out | jq -e ".execution.expiration == ${update2}"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with update events works (after watch)' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf) &&
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch5.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch5.out &&
	update1=$(expiration_add $jobid 100) &&
	update2=$(expiration_add $jobid 200) &&
	${WAITFILE} --count=3 --timeout=30 --pattern="expiration" watch5.out &&
	flux cancel $jobid &&
	wait $watchpid &&
	test $(cat watch5.out | wc -l) -eq 3 &&
	head -n1 watch5.out | jq -e ".execution.expiration == 0.0" &&
	head -n2 watch5.out | tail -n1 | jq -e ".execution.expiration == ${update1}" &&
	tail -n1 watch5.out | jq -e ".execution.expiration == ${update2}"
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch with update events works (before/after watch)' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf) &&
	update1=$(expiration_add $jobid 100) &&
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch6.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch6.out &&
	update2=$(expiration_add $jobid 200) &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch6.out &&
	flux cancel $jobid &&
	wait $watchpid &&
	test $(cat watch6.out | wc -l) -eq 2 &&
	head -n1 watch6.out | jq -e ".execution.expiration == ${update1}" &&
	tail -n1 watch6.out | jq -e ".execution.expiration == ${update2}"
'

# signaling the update watch tool with SIGUSR1 will cancel the stream
test_expect_success NO_CHAIN_LINT 'job-info: update watch can be canceled (single watcher)' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf) &&
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch7.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch7.out &&
	kill -s USR1 $watchpid &&
	wait $watchpid &&
	flux cancel $jobid &&
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
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf)
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch8A.out &
	watchpidA=$! &&
	wait_update_watchers $((watchers+1)) &&
	update1=$(expiration_add $jobid 100) &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch8A.out
	${UPDATE_WATCH} $jobid R > watch8B.out &
	watchpidB=$! &&
	wait_update_watchers $((watchers+2)) &&
	update2=$(expiration_add $jobid 200) &&
	${WAITFILE} --count=3 --timeout=30 --pattern="expiration" watch8A.out &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch8B.out
	${UPDATE_WATCH} $jobid R > watch8C.out &
	watchpidC=$! &&
	wait_update_watchers $((watchers+3)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch8C.out &&
	flux cancel $jobid &&
	wait $watchpidA &&
	wait $watchpidB &&
	wait $watchpidC &&
	test $(cat watch8A.out | wc -l) -eq 3 &&
	test $(cat watch8B.out | wc -l) -eq 2 &&
	test $(cat watch8C.out | wc -l) -eq 1 &&
	head -n1 watch8A.out | jq -e ".execution.expiration == 0.0" &&
	head -n2 watch8A.out | tail -n1 | jq -e ".execution.expiration == ${update1}" &&
	tail -n1 watch8A.out | jq -e ".execution.expiration == ${update2}" &&
	head -n1 watch8B.out | jq -e ".execution.expiration == ${update1}" &&
	tail -n1 watch8B.out | jq -e ".execution.expiration == ${update2}" &&
	tail -n1 watch8C.out | jq -e ".execution.expiration == ${update2}"
'

# signaling the update watch tool with SIGUSR1 will cancel the stream
test_expect_success NO_CHAIN_LINT 'job-info: update watch can be canceled (multiple watchers)' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf)
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch10A.out &
	watchpidA=$! &&
	wait_update_watchers $((watchers+1)) &&
	update1=$(expiration_add $jobid 100) &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch10A.out
	${UPDATE_WATCH} $jobid R > watch10B.out &
	watchpidB=$! &&
	wait_update_watchers $((watchers+2)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watch10B.out &&
	kill -s USR1 $watchpidA &&
	wait $watchpidA &&
	kill -s USR1 $watchpidB &&
	wait $watchpidB &&
	test_debug "echo watch10A: \"$(cat watch10A.out)\"" &&
	test_debug "echo watch10B: \"$(cat watch10B.out)\"" &&
	flux cancel $jobid &&
	test $(cat watch10A.out | wc -l) -eq 2 &&
	test $(cat watch10B.out | wc -l) -eq 1 &&
	head -n1 watch10A.out | jq -e ".execution.expiration == 0.0" &&
	tail -n1 watch10A.out | jq -e ".execution.expiration == ${update1}" &&
	cat watch10B.out | jq -e ".execution.expiration == ${update1}"
'

# If someone is already doing an update-watch on a jobid/key, lookup w/ update can
# return the cached info
test_expect_success NO_CHAIN_LINT 'job-info: lookup current returns cached R from update watch' '
	jobid=$(flux submit --wait-event=exec.shell.init sleep inf) &&
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watch9.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	update1=$(expiration_add $jobid 100) &&
	${WAITFILE} --count=2 --timeout=30 --pattern="expiration" watch9.out &&
	${INFO_LOOKUP} -c $jobid R > lookup9A.out &&
	${UPDATE_LOOKUP} $jobid R > lookup9B.out &&
	flux cancel $jobid &&
	wait $watchpid &&
	test $(cat watch9.out | wc -l) -eq 2 &&
	head -n1 watch9.out | jq -e ".execution.expiration == 0.0" &&
	tail -n1 watch9.out | jq -e ".execution.expiration == ${update1}" &&
	cat lookup9A.out | jq -e ".execution.expiration == ${update1}" &&
	cat lookup9B.out | jq -e ".execution.expiration == ${update1}"
'

#
# lookup w/ current and update-watch works with jobspec
#

# Usage: check_duration jobid VALUE
# Check and wait for duration to reach VALUE.
#
check_duration() {
       local jobid=$1
       local value=$2
       local i=0
       while (! ${INFO_LOOKUP} -c ${jobid} jobspec | jq -e ".attributes.system.duration == ${value}" \
	       && [ $i -lt 200 ] )
       do
	       sleep 0.1
	       i=$((i + 1))
       done
       if [ "$i" -eq "200" ]
       then
	       return 1
       fi
       return 0
}

test_expect_success 'job-info: lookup current works with jobspec' '
	jobid=$(flux submit --urgency=hold true) &&
	check_duration $jobid 0 &&
	flux update $jobid duration=100s &&
	check_duration $jobid 100.0 &&
	flux update $jobid duration=200s &&
	check_duration $jobid 200.0 &&
	flux cancel $jobid
'

test_expect_success NO_CHAIN_LINT 'job-info: update watch works with jobspec' '
	jobid=$(flux submit --urgency=hold true) &&
	flux update $jobid duration=100s &&
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid jobspec > watchjobspec.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="duration" watchjobspec.out &&
	flux update $jobid duration=200s &&
	${WAITFILE} --count=2 --timeout=30 --pattern="duration" watchjobspec.out &&
	flux cancel $jobid &&
	wait $watchpid &&
	test $(cat watchjobspec.out | wc -l) -eq 2 &&
	head -n1 watchjobspec.out | jq -e ".attributes.system.duration == 100.0" &&
	tail -n1 watchjobspec.out | jq -e ".attributes.system.duration == 200.0"
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
	test_must_fail ${INFO_LOOKUP} -u $jobid jobspec &&
	unset_userid &&
	flux cancel $jobid
'

test_expect_success 'job-info: non job owner cannot watch key' '
	jobid=`flux submit --wait-event=start sleep inf` &&
	set_userid 9999 &&
	test_must_fail ${UPDATE_WATCH} $jobid R &&
	test_must_fail ${UPDATE_WATCH} $jobid jobspec &&
	unset_userid &&
	flux cancel $jobid
'

# this test checks security on a second watcher, which is trying to
# access cached info
test_expect_success NO_CHAIN_LINT 'job-info: non job owner cannot watch key (second watcher)' '
	jobid=`flux submit --wait-event=exec.shell.init sleep inf`
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > watchsecurity.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" watchsecurity.out &&
	set_userid 9999 &&
	test_must_fail ${UPDATE_WATCH} $jobid R &&
	unset_userid &&
	flux cancel $jobid &&
	wait $watchpid
'

# lookup current cannot read cached watch data
test_expect_success NO_CHAIN_LINT 'job-info: non job owner cannot lookup key (piggy backed)' '
	jobid=`flux submit --wait-event=exec.shell.init sleep inf`
	watchers=$(get_update_watchers)
	${UPDATE_WATCH} $jobid R > lookupsecurity.out &
	watchpid=$! &&
	wait_update_watchers $((watchers+1)) &&
	${WAITFILE} --count=1 --timeout=30 --pattern="expiration" lookupsecurity.out &&
	set_userid 9999 &&
	test_must_fail ${INFO_LOOKUP} -c $jobid R &&
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
