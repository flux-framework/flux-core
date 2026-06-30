#!/bin/sh

test_description='Test job-manager private mode'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

owner_uid=$(id -u)
other_uid=$((owner_uid + 100))

submit_as_guest()
{
	flux run --dry-run "$@" | \
	  flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $other_uid \
	    >job.signed
	FLUX_HANDLE_USERID=$other_uid flux job submit --flags=signed job.signed
}

set_userid() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
	unset FLUX_HANDLE_USERID
	unset FLUX_HANDLE_ROLEMASK
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

job_manager_getinfo() {
	flux python -c "import flux; print(flux.Flux().rpc('job-manager.getinfo',{}).get_str())"
}

job_manager_getattr() {
	flux python -c "import flux; print(flux.Flux().rpc(\"job-manager.getattr\",{\"id\":"$1",\"attrs\":[\"$2\"]}).get_str())"
}

test_expect_success 'submit a job as instance owner' '
	jobid=$(flux submit true)
'
test_expect_success 'save jobid as integer' '
	flux job id --to=dec $jobid >jobid_int.out
'
test_expect_success FLUX_SECURITY 'submit a job as guest user' '
	guestid=$(submit_as_guest --urgency=hold hostname) &&
	flux job id --to=dec $guestid >guestid.out
'

test_expect_success 'private mode off: guest can call stats-get' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	flux module stats job-manager
'
test_expect_success 'private mode off: guest can call getinfo' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getinfo
'
test_expect_success FLUX_SECURITY 'private mode off: guest getattr on own job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getattr $(cat guestid.out) jobspec
'
test_expect_success 'private mode off: guest gets EINVAL for unknown job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>err.out &&
	grep -i "unknown job" err.out
'
test_expect_success 'private mode off: guest gets EPERM for other user job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec 2>err.out &&
	grep -i "guests can only access their own jobs" err.out
'

test_expect_success 'enable private mode' '
	enable_private_mode
'
test_expect_success 'private mode on: guest stats-get fails with EPERM' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux module stats job-manager 2>private-stats.err &&
	test_debug "cat private-stats.err" &&
	grep "not permitted" private-stats.err
'
test_expect_success 'private mode on: owner stats-get succeeds' '
	flux module stats job-manager >/dev/null
'
test_expect_success 'private mode on: guest getinfo fails with EPERM' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getinfo 2>private-getinfo.err &&
	test_debug "cat private-getinfo.err" &&
	grep "not permitted" private-getinfo.err
'
test_expect_success 'private mode on: owner getinfo succeeds' '
	job_manager_getinfo
'
test_expect_success 'private mode on: guest gets EPERM for unknown job (not EINVAL)' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>err.out &&
	test_must_fail grep -i "unknown job" err.out
'
test_expect_success 'private mode on: guest gets EPERM for another users job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec \
		2>private-getattr.err &&
	test_debug "cat private-getattr.err" &&
	grep "not permitted" private-getattr.err
'
test_expect_success FLUX_SECURITY 'private mode on: guest getattr on own job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getattr $(cat guestid.out) jobspec
'
test_expect_success 'private mode on: owner getattr succeeds' '
	job_manager_getattr $(cat jobid_int.out) jobspec
'

test_expect_success 'disable private mode' '
	disable_private_mode
'
test_expect_success 'private mode off: guest can call stats-get again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	flux module stats job-manager
'
test_expect_success 'private mode off: guest can call getinfo again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getinfo
'
test_expect_success 'private mode off: guest gets EINVAL for unknown job again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>err.out &&
	grep -i "unknown job" err.out
'
test_expect_success 'private mode off: guest gets EPERM for other user job again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec 2>err.out &&
	grep -i "guests can only access their own jobs" err.out
'
test_expect_success FLUX_SECURITY \
'private mode off: guest getattr on own job still works' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getattr $(cat guestid.out) jobspec
'


test_done
