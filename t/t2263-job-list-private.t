#!/bin/sh

test_description='Test job-list private mode'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc

owner_uid=$(id -u)
other_uid=$((owner_uid + 100))

fj_wait_event() {
	flux job wait-event --timeout=20 "$@"
}

wait_idsync() {
	local num=$1
	test_wait_until "flux module stats --parse idsync.waits job-list \
          && [ \$(flux module stats --parse idsync.waits job-list) -eq $num ]"
}

set_userid() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
	unset FLUX_HANDLE_USERID
	unset FLUX_HANDLE_ROLEMASK
}

count_jobs() {
	flux jobs -a -n -o "{id}" | wc -l
}

enable_private_mode() {
	flux config load <<-EOT
	[access]
	private-mode = true
	EOT
}

disable_private_mode() {
	flux config load </dev/null
}

#
# All jobs in this test are owned by the instance owner.  Guests are
# simulated by setting the message userid/rolemask, so a guest whose userid
# matches the job owner can see the jobs and a guest with a different userid
# cannot.  This exercises private mode without requiring flux-security.
#

test_expect_success 'submit jobs as instance owner' '
	flux submit --wait true >/dev/null &&
	flux submit --wait --job-name=secretjob true >/dev/null &&
	test $(count_jobs) -eq 2
'
test_expect_success 'save an owner job id' '
	flux jobs -a -n -o "{id}" | head -n1 >ownerjob
'
test_expect_success 'private mode off: guest with different uid sees all jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(count_jobs) -eq 2
'
test_expect_success 'enable private mode' '
	enable_private_mode
'
test_expect_success 'private mode on: guest with non-owner uid sees no jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(count_jobs) -eq 0
'
test_expect_success 'private mode on: guest with owner uid sees own jobs' '
	set_userid $owner_uid &&
	test_when_finished unset_userid &&
	test $(count_jobs) -eq 2
'
test_expect_success 'private mode on: owner sees all jobs' '
	test $(count_jobs) -eq 2
'
test_expect_success 'private mode on: guest cannot list-id another user job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux job list-ids $(cat ownerjob) >/dev/null
'
test_expect_success 'private mode on: guest can list-id own job' '
	set_userid $owner_uid &&
	test_when_finished unset_userid &&
	flux job list-ids $(cat ownerjob) >/dev/null
'
test_expect_success 'private mode on: owner can list-id any job' '
	flux job list-ids $(cat ownerjob) >/dev/null
'
test_expect_success 'private mode on: guest job-stats fails (EPERM)' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux jobs --stats
'
test_expect_success 'private mode on: guest module stats fails (EPERM)' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux module stats job-list
'
test_expect_success 'private mode on: owner job-stats succeeds' '
	flux jobs --stats >/dev/null &&
	flux module stats job-list >/dev/null
'
test_expect_success 'disable private mode' '
	disable_private_mode
'
test_expect_success 'private mode off: guest with non-owner uid sees all jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(count_jobs) -eq 2
'
test_expect_success 'private mode off: guest job-stats succeeds' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	flux jobs --stats
'

#
# Verify that an idsync waiter registered by a guest while private mode is
# on is denied when the job reaches the requested state.  This exercises the
# rc==0 branch in idsync_data_respond.
#
test_expect_success NO_CHAIN_LINT 'private mode on: list-id idsync waiter denied (rc==0 in idsync_data_respond)' '
	# Enable private mode first so auth is set before any idsync fires.
	enable_private_mode &&
        test_when_finished disable_private_mode &&
	# Pause job-list state events.  While paused, the job is not in the
	# job-list index, so a list-id request goes through the KVS-lookup
	# idsync path and parks in idsync_wait_valid.
	${RPC} job-list.job-state-pause 0 </dev/null &&
	newjob=$(flux submit hostname) &&
	# Wait for the job to complete so it is in KVS before the guest queries.
	fj_wait_event $newjob clean >/dev/null &&
	FLUX_HANDLE_USERID=$other_uid FLUX_HANDLE_ROLEMASK=0x2 \
	    flux job list-ids --wait-state=inactive $newjob \
	    >listid2.out 2>listid2.err &
	listid_pid=$! &&
	# Confirm waiter is registered before unpausing.
	wait_idsync 1 &&
	# Unpause: job-list processes backlog and idsync_data_respond fires.
	# With private mode on, it hits rc==0 and returns ENOENT.
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	test_must_fail wait $listid_pid &&
	grep -i "no such file or directory" listid2.err
'

#
# Verify that user-supplied constraints cannot bypass the private-mode
# auth constraint.  All jobs belong to the instance owner; a guest with
# a different uid should see zero jobs regardless of what constraint they
# supply.
#

test_expect_success 'enable private mode for constraint tests' '
	enable_private_mode
'
test_expect_success 'private mode on: --user=owner-uid returns no jobs for guest' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(flux jobs -a -n -o "{id}" --user=$owner_uid | wc -l) -eq 0
'
test_expect_success 'private mode on: --name=secretjob returns no jobs for guest' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(flux jobs -a -n -o "{id}" --name=secretjob | wc -l) -eq 0
'
test_expect_success 'private mode on: list with no constraint as guest sees no jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(flux python -c "
import flux
f = flux.Flux()
pay = {\"max_entries\": 1000, \"attrs\": [\"id\"]}
print(len(f.rpc(\"job-list.list\", pay).get()[\"jobs\"]))
") -eq 0
'
test_expect_success 'private mode on: direct userid constraint for owner returns no jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(flux python -c "
import flux, json
f = flux.Flux()
pay = {\"max_entries\": 1000, \"attrs\": [\"id\"],
	   \"constraint\": {\"userid\": [$owner_uid]}}
print(len(f.rpc(\"job-list.list\", pay).get()[\"jobs\"]))
") -eq 0
'
test_expect_success 'private mode on: or-constraint broadening returns no other-user jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(flux python -c "
import flux, json
f = flux.Flux()
pay = {\"max_entries\": 1000, \"attrs\": [\"id\"],
	   \"constraint\": {\"or\": [{\"userid\": [$owner_uid]},
	                             {\"userid\": [$other_uid]}]}}
print(len(f.rpc(\"job-list.list\", pay).get()[\"jobs\"]))
") -eq 0
'
test_expect_success 'private mode on: not-constraint cannot expose other-user jobs' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test $(flux python -c "
import flux, json
f = flux.Flux()
pay = {\"max_entries\": 1000, \"attrs\": [\"id\"],
	   \"constraint\": {\"not\": [{\"userid\": [$other_uid]}]}}
print(len(f.rpc(\"job-list.list\", pay).get()[\"jobs\"]))
") -eq 0
'
test_expect_success 'submit a running job for list-id private mode tests' '
	sleepjob=$(flux submit sleep 300) &&
	flux job list-ids --wait-state=RUN $sleepjob >/dev/null
'
test_expect_success 'private mode on: guest cannot list-id a running job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux job list-ids $sleepjob >/dev/null
'
# Submit without --wait so job-list may not have processed the event yet,
# giving the idsync KVS-lookup path a chance to be exercised.
test_expect_success 'private mode on: guest cannot list-id a freshly submitted job' '
	newjob=$(flux submit sleep 300) &&
	set_userid $other_uid &&
	test_must_fail flux job list-ids $newjob >/dev/null &&
	unset_userid &&
	flux cancel $newjob
'
test_expect_success 'cancel running job from list-id tests' '
	flux cancel $sleepjob
'
test_expect_success 'disable private mode after constraint tests' '
	disable_private_mode
'
test_done
