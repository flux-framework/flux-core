#
#  Check exec service with guest/owner
#

test_expect_success 'start a long-running guest job' '
        flux submit -n1 --wait-event=start sleep inf &&
	jobid=$(flux job last)
'
test_expect_success 'flux exec --jobid works as guest' '
	flux exec --jobid=$jobid true
'
test_expect_success 'flux exec --jobid fails as instance owner' '
	test_must_fail sudo -u flux flux exec --jobid=$jobid true 2>owner-exec.err &&
	test_debug "cat owner-exec.err" &&
	grep "request signature required" owner-exec.err
'
test_expect_success 'instance owner cannot exec guest job even when signing' '
	test_must_fail sudo -u flux flux exec --jobid=$jobid --sign true 2>owner-sign.err &&
	test_debug "cat owner-sign.err" &&
	grep "signing userid does not match" owner-sign.err
'
test_expect_success 'flux exec without --jobid works as instance owner' '
	sudo -u flux flux exec -r 0 true
'
test_expect_success 'flux exec without --jobid fails as guest' '
	test_must_fail flux exec -r 0 true
'
# Get the rexec service name (e.g. "501-shell-XXXX.rexec") for a job
job_rexec_service() {
	flux job eventlog --format=json -p exec $1 \
	    | jq -r 'select(.name == "shell.init") .context.service + ".rexec"'
}
test_expect_success 'get job shell rexec service and rank' '
	rank=$(flux jobs -no {ranks} $jobid) &&
	service=$(job_rexec_service $jobid)
'
test_expect_success 'flux exec --jobid with stdin forwarding works' '
	echo "hello" | flux exec --jobid=$jobid cat >stdin.out &&
	test_debug "cat stdin.out" &&
	grep "hello" stdin.out
'
test_expect_success 'unsigned direct exec to shell rexec service is rejected' '
	test_must_fail flux exec -r$rank --service=$service hostname 2>direct.err &&
	test_debug "cat direct.err" &&
	grep "request signature required" direct.err
'
#
# `flux sproc` tests against the job shell rexec service.
# In the system instance security.owner != getuid(), so flux.subprocess
# auto-signing (sign=None) should activate for all shell rexec requests.
#
test_expect_success 'start labeled background process in guest job shell' '
	flux exec --jobid=$jobid --bg --waitable --label=sproc-test sleep inf
'
test_expect_success 'flux sproc ps on guest job shows process' '
	flux sproc ps --service $service --rank $rank >ps.out &&
	test_debug "cat ps.out" &&
	grep "sproc-test" ps.out
'
test_expect_success 'flux sproc ps on guest job fails as instance owner' '
	test_must_fail sudo -u flux \
	    flux sproc ps --service $service --rank $rank 2>ps-owner.err &&
	test_debug "cat ps-owner.err" &&
	grep "request signature required" ps-owner.err
'
test_expect_success 'flux sproc kill --wait on guest job works' '
	test_expect_code 143 \
	    flux sproc kill --wait --service $service --rank $rank 15 sproc-test
'
test_expect_success 'start another labeled background process in guest job shell' '
	flux exec --jobid=$jobid --bg --waitable --label=wait-test sleep inf
'
test_expect_success 'flux sproc wait on guest job works' '
	flux sproc kill --service $service --rank $rank 15 wait-test &&
	test_expect_code 143 \
	    flux sproc wait --service $service --rank $rank wait-test
'
#
# Tests using an independent user (user1) verify that a different tenant
# cannot reach a guest job's shell, even by signing.  These go directly to
# the shell service since another user can't fetch service and rank from
# the job eventlog.
#
sudo -u user1 id && test_set_prereq HAVE_USER1`
test_expect_success HAVE_USER1 'another user unsigned exec to shell is rejected' '
	test_must_fail sudo -u user1 \
	    flux exec -r$rank --service=$service hostname 2>user1-exec.err &&
	test_debug "cat user1-exec.err" &&
	grep "request signature required" user1-exec.err
'
test_expect_success HAVE_USER1 'another user cannot exec shell even when signing' '
	test_must_fail sudo -u user1 \
	    flux exec -r$rank --service=$service --sign hostname 2>user1-sign.err &&
	test_debug "cat user1-sign.err" &&
	grep "signing userid does not match" user1-sign.err
'
test_expect_success HAVE_USER1 'another user cannot list processes in shell' '
	test_must_fail sudo -u user1 \
	    flux sproc ps --service $service --rank $rank 2>user1-ps.err &&
	test_debug "cat user1-ps.err" &&
	grep "request signature required" user1-ps.err
'
test_expect_success 'cancel long-running job' '
	flux cancel $jobid
'
