#!/bin/sh
test_description='Test flux update command'

. $(dirname $0)/sharness.sh

if flux job submit --help 2>&1 | grep -q sign-type; then
	test_set_prereq HAVE_FLUX_SECURITY
fi

test_under_flux 1 job -Slog-stderr-level=1

runas_guest() {
	local userid=$(($(id -u)+1))
        FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

submit_held_job_as_guest()
{
	local duration=$1
	local userid=$(($(id -u)+1))
        flux run --dry-run -t $duration true | \
          flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $userid \
            >job.signed
        FLUX_HANDLE_USERID=$userid \
          flux job submit --flags=signed --urgency=0 job.signed
}

test_expect_success 'flux update requires jobid and keyval args' '
	test_expect_code 2 flux update 1234 &&
	test_expect_code 2 flux update
'
test_expect_success 'submit jobs for testing' '
	inactive_jobid=$(flux submit --wait-event=clean true) &&
	jobid=$(flux submit --urgency=hold sleep 60)
'
test_expect_success 'invalid jobid fails' '
	test_expect_code 1 flux update f123 duration=10m 2>invalid-jobid.err &&
	test_debug "cat invalid-jobid.err" &&
	grep "unknown job id" invalid-jobid.err
'
test_expect_success 'update to inactive job fails' '
	test_expect_code 1 flux update $inactive_jobid duration=1m \
		2>inactive-jobid.err &&
	test_debug "cat inactive-jobid.err" &&
	grep "job is inactive" inactive-jobid.err
'
test_expect_success 'invalid jobspec key cannot be updated' '
	test_expect_code 1 flux update $jobid foo=bar
'
test_expect_success 'guests can only update their own jobs' '
	test_expect_code 1 runas_guest flux update $jobid duration=1h \
		2>invalid-user.err &&
	test_debug "cat invalid-user.err" &&
	grep "guests may only update their own jobs" invalid-user.err
'
test_expect_success 'update of unlimited duration with relative value fails' '
	test_expect_code 1 flux update $jobid duration=+1m  &&
	test_expect_code 1 flux update $jobid duration=-1m
'
test_expect_success 'update request for negative duration fails' '
	echo "{\"id\": $(flux job id $jobid),\
	       \"updates\": {\"attributes.system.duration\": -1.0}\
              }" \
	    | ${FLUX_BUILD_DIR}/t/request/rpc job-manager.update 22 # EINVAL
'
test_expect_success 'update request with invalid duration type fails' '
	echo "{\"id\": $(flux job id $jobid),\
	       \"updates\": {\"attributes.system.duration\": "foo"}\
              }" \
	    | ${FLUX_BUILD_DIR}/t/request/rpc job-manager.update 71 # EPROTO
'
test_expect_success 'update of duration of pending job works' '
	flux update $jobid duration=1m &&
	flux job eventlog $jobid \
		| grep jobspec-update \
		| grep duration=60
'
test_expect_success 'update of duration accepts relative values' '
	flux update --dry-run $jobid duration=+1m \
	  | jq ".\"attributes.system.duration\" == 120." &&
	flux update --dry-run $jobid duration=-30s \
	  | jq ".\"attributes.system.duration\" == 30."
'
test_expect_success 'update with multiple keys fails if one key fails' '
	test_expect_code 1 flux update $jobid duration=12345 name=foo
'
test_expect_success 'update of duration to inf sets duration to 0' '
	flux update --dry-run $jobid duration=inf \
	  | jq ".\"attributes.system.duration\" == 0."
'
test_expect_success 'flux update rejects duration <= 0 fails' '
	test_expect_code 1 flux update $jobid duration=-1h
'
test_expect_success 'update affects duration of running job' '
	flux update $jobid duration=0.1s &&
	flux job urgency $jobid default &&
	flux job wait-event -t 30 -m type=timeout $jobid exception &&
	flux job info $jobid R \
	 | jq -e "(.execution|(.expiration - .starttime)*10 + 0.5 | floor) == 1"
'
test_expect_success 'add a duration limit and submit a held job' '
	echo policy.limits.duration=\"1m\" | flux config load &&
	jobid=$(flux submit --urgency=hold -t 1m true)
'
test_expect_success 'instance owner can adjust duration past limits' '
	flux update $jobid duration=1h &&
	flux job eventlog $jobid \
		| grep jobspec-update \
		| grep duration=3600
'
test_expect_success FLUX_SECURITY 'guest update of their own job works' '
	guest_jobid=$(submit_held_job_as_guest 1m) &&
	runas_guest flux update -v $guest_jobid duration=1m  &&
	flux job eventlog $guest_jobid \
		| grep jobspec-update \
		| grep duration=60
'
test_expect_success FLUX_SECURITY 'guest cannot update job duration past limit' '
	test_expect_code 1 runas_guest flux update -v $guest_jobid duration=1h
'
test_expect_success 'instance owner can adjust duration past limits' '
	flux update $jobid duration=1h &&
	flux job eventlog $jobid \
		| grep jobspec-update \
		| grep duration=3600
'
test_expect_success FLUX_SECURITY 'instance owner can adjust guest job duration past limits' '
	flux update $guest_jobid duration=1h &&
	flux job eventlog $guest_jobid \
		| grep jobspec-update \
		| grep duration=3600
'
test_expect_success FLUX_SECURITY 'guest job is now immutable' '
	test_expect_code 1 runas_guest \
		flux update -v $guest_jobid duration=1m \
			2>immutable.err &&
	test_debug "cat immutable.err" &&
	grep immutable immutable.err
'
test_expect_success 'adjust duration so future tests pass validation' '
	flux update $jobid duration=1m
'
test_expect_success 'reload update-duration plugin with owner-allow-any=0' '
	flux jobtap remove .update-duration &&
	flux jobtap load .update-duration owner-allow-any=0
'
test_expect_success 'update duration above policy limit now fails' '
	test_expect_code 1 flux update $jobid duration=1h 2>limit.err &&
	test_debug "cat limit.err" &&
	grep "requested duration.*exceeds policy limit" limit.err
'
test_expect_success 'update fails for running job' '
	jobid=$(flux submit -t1m --wait-event=start sleep 60) &&
	test_expect_code 1 flux update $jobid duration=90s 2>run.err &&
	test_debug "cat run.err" &&
	grep "duration update of running job requires instance owner" run.err
'
test_expect_success 'update of attributes.system.test fails' '
	test_expect_code 1 flux update $jobid test=foo
'
test_expect_success 'load update-test jobtap plugin' '
	PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs &&
	flux jobtap load --remove=all $PLUGINPATH/update-test.so
'
test_expect_success 'now update of attributes.system.test works' '
	flux update $jobid test=foo-update &&
	flux job eventlog $jobid \
		| grep jobspec-update \
		| grep foo-update
'
test_expect_success 'update-test plugin can reject updates' '
	test_expect_code 1 flux update $jobid test=fail-test 2>fail-test.err &&
	test_debug "cat fail-test.err" &&
	grep "rejecting update" fail-test.err
'
test_expect_success 'multiple keys can be updated successfully' '
	flux update -v $jobid test=ok test2=ok2 &&
	flux job eventlog $jobid &&
	flux job eventlog $jobid \
		| grep jobspec-update \
		| grep "test=\"ok\" attributes.system.test2=\"ok2\""
'
test_done
