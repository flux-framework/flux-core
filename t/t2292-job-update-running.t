#!/bin/sh
test_description='Test update of running jobs'

. $(dirname $0)/sharness.sh

if flux job submit --help 2>&1 | grep -q sign-type; then
	test_set_prereq HAVE_FLUX_SECURITY
fi

test_under_flux 4 job -Slog-stderr-level=1

export FLUX_URI_RESOLVE_LOCAL=t
runas_guest() {
	local userid=$(($(id -u)+1))
        FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

submit_job_as_guest()
{
	local duration=$1
	local userid=$(($(id -u)+1))
        flux run --dry-run -t $duration \
	  --setattr=exec.test.run_duration=\"600\" \
	  sleep inf | \
          flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $userid \
            >job.signed
        FLUX_HANDLE_USERID=$userid \
          flux job submit --flags=signed job.signed
}

# Usage: job_manager_get_R ID
# This needs to be a shell script since it will be run under flux-proxy(1):
cat <<'EOF' >job_manager_get_R
#!/bin/sh
flux python -c \
"import flux; \
 payload = {\"id\":$(flux job id $1),\"attrs\":[\"R\"]}; \
 print(flux.Flux().rpc(\"job-manager.getattr\", payload).get_str()) \
"
EOF
chmod +x job_manager_get_R

export PATH=$(pwd):$PATH

# some tests grep for job id via flux proxy.  To ensure job id is consistent
# across all tests and instances, set FLUX_F58_FORCE_ASCII to true.
export FLUX_F58_FORCE_ASCII=1

job_manager_get_expiration() {
	job_manager_get_R $1 | jq .R.execution.expiration
}
job_manager_get_starttime() {
	job_manager_get_R $1 | jq .R.execution.starttime
}

test_expect_success 'configure testexec to allow guest access' '
	flux config load <<-EOF
	[exec.testexec]
	allow-guests = true
	EOF
'
test_expect_success 'instance owner can adjust expiration of their own job' '
	jobid=$(flux submit --wait-event=start -t5m sleep 300) &&
	expiration=$(job_manager_get_expiration $jobid) &&
	test_debug "echo expiration=$expiration" &&
	flux update $jobid duration=+1m &&
	job_manager_get_expiration $jobid | jq -e ". == $expiration + 60"
'
test_expect_success 'duration update with expiration in past fails' '
	test_must_fail flux update $jobid duration=10ms 2>err.log &&
	grep past err.log
'
#  Test that job execution systems sees an expiration update by
#  waiting for an expected log message from the job-exec module
#
test_expect_success 'duration update is processed by execution system' '
	flux update $jobid duration=+1m &&
	$SHARNESS_TEST_SRCDIR/scripts/dmesg-grep.py \
		-vt 30 "job-exec.*updated expiration of $jobid" &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
#  Test that the job shell correctly processes an expiration update.
#  Set up the job shell to send SIGTERM to the job 60s before expiration.
#  for a job with a 5m duration. Then adjust duration such that expiration
#  will occur 30s from now, and ensure the job shell picks up the update and
#  sends SIGTERM immediately.
#
test_expect_success 'duration update is processed by job shell' '
	jobid=$(flux submit --wait-event start \
		-o verbose --signal=SIGTERM@60 -t5 sleep 300) &&
	duration=$(job_manager_get_starttime $jobid | jq "now - . + 30") &&
	flux update $jobid duration=$duration &&
	test_must_fail_or_be_terminated \
		flux job attach $jobid >shell.log 2>&1 &&
	test_debug "cat shell.log" &&
	grep "Will send SIGTERM to job in 0\.0" shell.log &&
	grep "sending SIGTERM" shell.log
'
test_expect_success 'duration can be set to unlimited for a running job' '
	jobid=$(flux submit --wait-event=start -t5m sleep 300) &&
	expiration=$(job_manager_get_expiration $jobid) &&
	test_debug "echo expiration=$expiration" &&
	flux update $jobid duration=0 &&
	job_manager_get_expiration $jobid | jq -e ". == 0." &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'
test_expect_success HAVE_FLUX_SECURITY 'duration update of running job is denied for guest' '
	jobid=$(submit_job_as_guest 5m) &&
	flux job wait-event -vt 30 $jobid start &&
	test_must_fail runas_guest flux update $jobid duration=+1m &&
	flux cancel $jobid
'
test_expect_success HAVE_FLUX_SECURITY 'duration update of running guest job is allowed for owner' '
	jobid=$(submit_job_as_guest 5m) &&
	flux job wait-event -vt 30 $jobid start &&
	expiration=$(job_manager_get_expiration $jobid) &&
	flux update $jobid duration=+1m &&
	job_manager_get_expiration $jobid | jq -e ". == $expiration + 60" &&
	flux cancel $jobid
'
#  Set module debug flag 0x4 on sched-simple so it will deny sched.expiration
#  RPCs:
test_expect_success 'expiration update can be denied by scheduler' '
	flux module debug --set 4 sched-simple &&
	jobid=$(flux submit --wait-event=start -t 5m sleep 300) &&
	test_must_fail flux update $jobid duration=+10m >sched-deny.out 2>&1 &&
	test_debug "cat sched-deny.out" &&
	grep "scheduler refused" sched-deny.out &&
	flux cancel $jobid &&
	flux module debug --clear sched-simple
'
#  Set module debug flag on job-exec so it will respond with error to the
#  exec.expiration RPC from job manager:
test_expect_success 'failure in exec.expiration RPC results in nonfatal job exception' '
	flux module debug --set 1 job-exec &&
	jobid=$(flux submit --wait-event=start -t 5m sleep 300) &&
	flux update $jobid duration=1m &&
	flux job wait-event -vt 30 -m severity=1 $jobid exception &&
	flux cancel $jobid
'
subinstance_get_R() {
	flux proxy $1 flux kvs get resource.R
}
subinstance_get_expiration() {
	subinstance_get_R $1 | jq .execution.expiration
}
# Usage: subinstance_get_duration ID JOBID
subinstance_get_job_duration() {
	flux proxy $1 job_manager_get_R $2 |
		jq '.R.execution | .expiration - .starttime'
}
subinstance_get_job_expiration() {
	flux proxy $1 job_manager_get_R $2 | jq '.R.execution.expiration'
}

# Run the following tests with and without `--conf=resource.rediscover=true`
# N.B.: to keep indent sane, the contents of this for loop are not indented:
for conf_arg in "" "--conf=resource.rediscover=true"; do

if test -n "$conf_arg"; then
    label="rediscover: "
fi

#
#  The following tests ensure that an expiration modification of a job
#  is propagated to child jobs when appropriate. The tests may be somewhat
#  difficult to follow, so are summarized in comments:
#
# 1. submit an instance with 5m time limit
# 2. submit a job in that instance with no duration - its expiration should
#    be set to that of the instance. (duration <5m)
# 3. Submit a job with a set duration for testing (duration 4m)
# 4. update duration of instance to 10m (+5m)
# 5. wait for instance resource module to post resource-update event
#    which indicates the expiration update is reflected in resource.R.
# 6. wait for sched-simple to register expiration update by checking
#    for expected log message
# 7. Ensure subinstance now reflects updated expiration in resource.R.
# 8. Ensure new expiration is 300s greater than old expiration
#
test_expect_success "${label}expiration update is detected by subinstance" '
	id=$(flux alloc --bg -t5m -n4 $conf_arg) &&
	exp1=$(subinstance_get_expiration $id) &&
	test_debug "echo instance expiration is $exp1" &&
	id1=$(flux proxy $id flux submit sleep 300) &&
	id2=$(flux proxy $id flux submit -t4m sleep 300) &&
	tleft1=$(flux proxy $id flux job timeleft $id1) &&
	duration1=$(subinstance_get_job_duration $id $id1) &&
	duration2=$(subinstance_get_job_duration $id $id2) &&
	test_debug "echo initial duration of job1 is $duration1" &&
	test_debug "echo initial timeleft of job1 is $tleft1" &&
	echo $duration1 | jq -e ". < 300" &&
	echo $duration2 | jq -e ". == 240" &&
	test_debug "echo updating duration of alloc job +5m" &&
	flux update $id duration=+5m &&
	test_debug "echo waiting for resource-update event" &&
	flux proxy $id flux kvs eventlog wait-event -vt 30 \
		resource.eventlog resource-update &&
	test_debug "echo waiting for scheduler to see updated expiration" &&
	flux proxy $id $SHARNESS_TEST_SRCDIR/scripts/dmesg-grep.py \
		-vt 30 \"sched-simple.*expiration updated\" &&
	exp2=$(subinstance_get_expiration $id) &&
	subinstance_get_R $id &&
	test_debug "echo expiration updated from $exp1 to $exp2" &&
	echo $exp2 | jq -e ". == $exp1 + 300"
'
#
#  9. Submit job to previous instance and ensure its expiration matches
#     the updated value
# 10. Ensure flux-job timeleft returns > 5m for the new job
# 11. Wait for expected resource-update event to be propagated to the first job
# 12. Ensure the timeleft of the first job is now > 5m
#
test_expect_success "${label}instance expiration update propagates to jobs" '
	id3=$(flux proxy $id flux submit --wait-event=start sleep 300) &&
	tleft3=$(flux proxy $id flux job timeleft $id3) &&
	duration3=$(subinstance_get_job_duration $id $id3) &&
	test_debug "echo timeleft of job submitted after update is $tleft3" &&
	test_debug "echo duration of job submitted after update is $duration3" &&
	echo $duration3 | jq -e ". > 300" &&
	flux proxy $id flux job wait-event -vt 20 \
		$id1 resource-update &&
	tleft1=$(flux proxy $id flux job timeleft $id1) &&
	duration1=$(subinstance_get_job_duration $id $id1) &&
	test_debug "echo timeleft of job submitted extended to $tleft1" &&
	test_debug "echo duration of job submitted extended to $duration1" &&
	echo $duration1 | jq -e ". > 300"
'
#
# 13. Check that expiration of job submitted with a duration is untouched.
#
test_expect_success "${label}instance expiration is not propagated to jobs with duration" '
	tleft2=$(flux proxy $id flux job timeleft $id2) &&
	duration2=$(subinstance_get_job_duration $id $id2) &&
	test_debug "echo timeleft of job with duration is now $tleft2" &&
	test_debug "echo duration of same job is now $duration2" &&
	echo $duration2 | jq -e ". == 240"
'
#
# 14. Now update job expiration to unlimited and ensure jobs have unlimited
#     duration as well:
#
test_expect_success "${label}instance expiration can be updated to unlimited" '
	test_debug "echo updating instance duration to inf" &&
	flux update $id duration=inf &&
	test_debug "echo waiting for second job resource-update event" &&
	flux proxy $id flux job wait-event -c 2 -vt 20 \
		$id1 resource-update &&
	exp=$(subinstance_get_job_expiration $id $id1) &&
	test_debug "echo job1 expiration is now $exp" &&
	echo $exp | jq -e ". == 0" &&
	flux proxy $id $SHARNESS_TEST_SRCDIR/scripts/dmesg-grep.py \
		-vt 30 \"job-exec.*updated expiration of $id1 to 0\.0\"
'
#
# 15. Now update job expiration to 1h and ensure job expiration is reduced
#
test_expect_success "${label}instance expiration can be decreased from unlimited" '
	test_debug "echo updating instance duration to 1h" &&
	flux update $id duration=1h &&
	test_debug "echo waiting for third job resource-update event" &&
	flux proxy $id flux job wait-event -c 3 -vt 20 \
		$id1 resource-update &&
	duration=$(subinstance_get_job_duration $id $id1) &&
	test_debug "echo job1 duration is now $duration" &&
	echo $duration | jq -e ". < 3600 and . > 0" &&
	flux proxy $id $SHARNESS_TEST_SRCDIR/scripts/dmesg-grep.py \
		-vt 30 \"job-manager.*expiration of $id1.*-inf\"
'
#
# 16. Now update job expiration to 30m. Job expiration should be reduced again.
#
test_expect_success "${label}instance expiration can be decreased again" '
	test_debug "echo updating instance duration to 30m" &&
	flux update $id duration=-.5h &&
	test_debug "echo waiting for fourth job resource-update event" &&
	flux proxy $id flux job wait-event -c 4 -vt 20 \
		$id1 resource-update &&
	duration=$(subinstance_get_job_duration $id $id1) &&
	test_debug "echo job1 duration is now $duration" &&
	echo $duration | jq -e ". < 1800 and . > 0" &&
	flux proxy $id $SHARNESS_TEST_SRCDIR/scripts/dmesg-grep.py \
		-vt 30 \"job-manager.*expiration of $id1.*-1800\"
'
test_expect_success "${label}shutdown test instance" '
	flux proxy $id flux cancel --all &&
	flux shutdown --quiet $id &&
	flux job wait-event $id clean
'
done # END for conf_arg in ...
test_done
