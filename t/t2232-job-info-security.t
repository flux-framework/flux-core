#!/bin/sh

test_description='Test flux job info service security'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

# Usage: submit_as_alternate_user cmd...
submit_as_alternate_user()
{
	FAKE_USERID=42
        flux run --dry-run "$@" | \
          flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
            >job.signed
        FLUX_HANDLE_USERID=$FAKE_USERID \
          flux job submit --flags=signed job.signed
}

set_userid() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
	unset FLUX_HANDLE_USERID
	unset FLUX_HANDLE_ROLEMASK
}

test_expect_success 'configure to allow guests to use test execution' '
	flux config load <<-EOT
	exec.testexec.allow-guests = true
	EOT
'

test_expect_success 'submit a job as instance owner and wait for clean' '
	jobid=$(flux submit -n1 true) &&
	flux job wait-event $jobid clean &&
	echo $jobid >jobid.owner
'

test_expect_success FLUX_SECURITY 'submit a job as guest and wait for clean' '
	jobid=$(submit_as_alternate_user -n1 true) &&
	flux job wait-event $jobid clean &&
	echo $jobid >jobid.guest
'

#
# job lookup (via eventlog)
#

test_expect_success 'flux job eventlog works as instance owner' '
	flux job eventlog $(cat jobid.owner)
'

test_expect_success FLUX_SECURITY 'flux job eventlog works as guest' '
	set_userid 42 &&
	flux job eventlog $(cat jobid.guest)
'

test_expect_success FLUX_SECURITY 'flux job eventlog fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job eventlog $(cat jobid.guest)
'

test_expect_success FLUX_SECURITY 'flux job eventlog as owner works on guest job' '
	unset_userid &&
	flux job eventlog $(cat jobid.guest)
'

test_expect_success 'flux job eventlog -p exec works as instance owner' '
	unset_userid &&
	flux job eventlog -p exec $(cat jobid.owner)
'

test_expect_success FLUX_SECURITY 'flux job eventlog -p exec works as guest' '
	set_userid 42 &&
	flux job eventlog -p exec $(cat jobid.guest)
'

test_expect_success FLUX_SECURITY 'flux job eventlog -p exec fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job eventlog -p exec $(cat jobid.guest)
'

#
# job info lookup (via 'info')
#

test_expect_success 'flux job info eventlog works as instance owner' '
	unset_userid &&
	flux job info $(cat jobid.owner) eventlog
'

test_expect_success FLUX_SECURITY 'flux job info eventlog works as guest ' '
	set_userid 42 &&
	flux job info $(cat jobid.guest) eventlog
'

test_expect_success FLUX_SECURITY 'flux job info eventlog as owner works on guest job' '
	unset_userid &&
	flux job info $(cat jobid.guest) eventlog
'

test_expect_success FLUX_SECURITY 'flux job info eventlog fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job info $(cat jobid.guest) eventlog
'

test_expect_success 'flux job info jobspec works as instance owner' '
	unset_userid &&
	flux job info $(cat jobid.owner) jobspec
'

test_expect_success FLUX_SECURITY 'flux job info jobspec works as guest' '
	unset_userid &&
	set_userid 42 &&
	flux job info $(cat jobid.guest) jobspec
'

test_expect_success FLUX_SECURITY 'flux job info jobspec fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job info $(cat jobid.guest) jobspec
'

test_expect_success 'flux job info foobar fails as instance owner' '
	unset_userid &&
	test_must_fail flux job info $(cat jobid.owner) foobar
'

test_expect_success 'flux job info foobar fails as guest' '
	set_userid 42 &&
	test_must_fail flux job info $(cat jobid.guest) foobar
'

test_expect_success 'flux job info foobar fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job info $(cat jobid.guest) foobar
'

#
# job eventlog watch (via wait-event)
#

test_expect_success 'flux job wait-event works as instance owner' '
	unset_userid &&
	flux job wait-event $(cat jobid.owner) submit
'

test_expect_success FLUX_SECURITY 'flux job wait-event works guest' '
	set_userid 42 &&
	flux job wait-event $(cat jobid.guest) submit
'

test_expect_success FLUX_SECURITY 'flux job wait-event fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job wait-event $(cat jobid.guest) submit
'

test_expect_success 'flux job wait-event -p exec works as instance owner' '
	unset_userid &&
	flux job wait-event -p exec $(cat jobid.owner) done
'

test_expect_success FLUX_SECURITY 'flux job wait-event -p exec as guest' '
	set_userid 42 &&
	flux job wait-event -p exec $(cat jobid.guest) done
'

test_expect_success FLUX_SECURITY 'flux job wait-event -p exec fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job wait-event -p exec $(cat jobid.guest) done
'

test_expect_success 'flux job wait-event -p exec on running job works as instance owner' '
	unset_userid &&
	jobid=$(flux submit -n1 sleep 300) &&
	flux job wait-event -p exec $jobid init &&
	echo $jobid >jobid.running
'

test_expect_success 'cancel job' '
	unset_userid &&
	flux cancel $(cat jobid.running)
'

test_expect_success FLUX_SECURITY 'flux job wait-event -p exec on running job works as guest' '
	unset_userid &&
	jobid=$(submit_as_alternate_user -n1 \
	    --setattr=system.exec.test.run_duration=5m true) &&
	set_userid 42 &&
	flux job wait-event -p exec $jobid init &&
	echo $jobid >jobid-guest.running
'

test_expect_success FLUX_SECURITY 'flux job wait-event -p exec on running job fails as wrong guest' '
	unset_userid &&
	set_userid 9999 &&
	test_must_fail flux job wait-event -p exec $(cat jobid-guest.running) init
'

test_expect_success FLUX_SECURITY 'cancel job' '
	unset_userid &&
	flux cancel $(cat jobid-guest.running)
'

#
# job info invalid eventlog formatting corner case coverage
#

# for these tests we need to create a fake job eventlog in the KVS

# value of "dummy" irrelevant here, just need to lookup something

test_expect_success 'create empty eventlog for job' '
	jobpath=`flux job id --to=kvs 123456789` &&
	flux kvs put "${jobpath}.eventlog"="" &&
	flux kvs put "${jobpath}.dummy"="foobar"
'

test_expect_success 'flux job info dummy fails as owner' '
	test_must_fail flux job info $jobpath dummy 2>malevent.err &&
	grep "error parsing eventlog" malevent.err
'

test_expect_success 'flux job info dummy fails as guest' '
	set_userid 42 &&
        test_must_fail flux job info 123456789 dummy 2>malevent2.err &&
	grep "error parsing eventlog" malevent2.err
'

test_expect_success 'flux job info fails on malformed eventlog (not json)' '
	unset_userid &&
	jobpath=`flux job id --to=kvs 123456789` &&
	flux kvs put "${jobpath}.eventlog"="foobar" &&
        test_must_fail flux job info 123456789 dummy 2>malevent3.err &&
	grep "error parsing eventlog" malevent3.err
'

test_expect_success 'flux job info fails on malformed eventlog (no submit event)' '
        submitstr="{\"timestamp\":123.4,\"name\":\"submit\"}" &&
	jobpath=`flux job id --to=kvs 123456789` &&
	echo $submitstr | flux kvs put --raw "${jobpath}.eventlog"=- &&
        test_must_fail flux job info 123456789 dummy 2>malevent4.err &&
	grep "error parsing eventlog" malevent4.err
'

test_expect_success 'flux job info fails on malformed eventlog (no submit userid)' '
        submitstr="{\"timestamp\":123.4,\"name\":\"submit\",\"context\":{}}" &&
	jobpath=`flux job id --to=kvs 123456789` &&
	echo $submitstr | flux kvs put --raw "${jobpath}.eventlog"=- &&
        test_must_fail flux job info 123456789 dummy 2>malevent5.err &&
	grep "error parsing eventlog" malevent5.err
'

test_expect_success 'flux job info fails on malformed eventlog (random garbage)' '
	jobpath=`flux job id --to=kvs 123456789` &&
        dd if=/dev/urandom bs=64 count=1 > binary.out &&
	flux kvs put --raw "${jobpath}.eventlog"=- < binary.out &&
        test_must_fail flux job info 123456789 dummy 2>malevent6.err &&
	grep "error parsing eventlog" malevent6.err
'

test_done
