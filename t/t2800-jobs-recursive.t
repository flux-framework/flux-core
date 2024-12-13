#!/bin/sh

test_description='Test flux jobs command with --recursive'

. $(dirname $0)/sharness.sh

test_under_flux 2 job
flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

export FLUX_PYCLI_LOGLEVEL=10
export FLUX_URI_RESOLVE_LOCAL=t
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py --line-buffer -f asciicast"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
sign_as=${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py

submit_fake_user_instance()
{
	FAKE_USERID=42
	test_debug "echo running flux run $@ as userid $FAKE_USERID"
	flux run --dry-run \
		--setattr=system.exec.test.run_duration=1d hostname | \
		flux python $sign_as $FAKE_USERID \
		>job.signed &&
	FLUX_HANDLE_USERID=$FAKE_USERID \
		flux job submit --flags=signed job.signed >altid &&
	flux job memo $(cat altid) uri=local:///dev/null
}


#  Start a child instance that immediately exits, so that we can test
#   that `flux jobs -R` doesn't error on child instances that are no
#   longer running.
#
#  Then, start a job hiearachy with 2 child instances, each of which
#   run a sleep job, touch a ready.<id> file, then block waiting for
#   the sleep job to finish.
#
test_expect_success 'start a recursive job' '
	id=$(flux submit flux start true) &&
	rid=$(flux submit -n2 \
		flux start \
		flux submit --wait --cc=1-2 flux start \
			"flux submit sleep 300 && \
			 touch ready.\$FLUX_JOB_CC && \
			 flux queue idle") &&
	flux job wait-event $id clean
'
test_expect_success 'allow guest user access to testexec' '
	flux config load <<-EOF
	[exec.testexec]
	allow-guests = true
	EOF
'
test_expect_success FLUX_SECURITY 'submit fake instance job as another user' '
	submit_fake_user_instance
'
blue_line_count() {
        grep -c -o "\\u001b\[01\;34m" $1 || true
}
test_expect_success 'instance jobs are highlighted in blue' '
	$runpty -o jobs.cast flux jobs  &&
	test $(blue_line_count jobs.cast) -eq 1
'
test_expect_success 'wait for hierarchy to be ready' '
	flux getattr broker.pid &&
	$waitfile -t 60 ready.1 &&
	$waitfile -t 60 ready.2
'
test_expect_success 'flux jobs --recursive works' '
	flux jobs --recursive > recursive.out &&
	test_debug "cat recursive.out" &&
	test $(grep -c : recursive.out) -eq 3
'
test_expect_success 'flux jobs --recursive avoids other user jobs by default' '
	flux jobs -A --recursive > recursive.out &&
	test_debug "cat recursive.out" &&
	test $(grep -c : recursive.out) -eq 3
'
test_expect_success 'flux jobs -o {id} --recursive works' '
	flux jobs -o {id.f58} --recursive > recursive-o.out &&
	test_debug "cat recursive.out" &&
	test $(grep -c : recursive.out) -eq 3
'
test_expect_success 'flux jobs --recursive --stats works' '
	flux jobs --recursive --stats > recursive-stats.out &&
	test_debug "cat recursive-stats.out" &&
	test $(grep -c running recursive-stats.out) -eq 4
'
test_expect_success 'flux jobs --recursive --level  works' '
	flux jobs --recursive --level=1 >recursive-level.out &&
	test_debug "cat recursive-level.out" &&
	test $(grep -c : recursive-level.out) -eq 1
'
test_expect_success 'flux jobs --recursive JOBID works' '
	flux jobs --recursive $rid > recursive-jobid.out &&
	test_debug "cat recursive-jobid.out" &&
	test $(grep -c : recursive-jobid.out) -eq 3
'
test_expect_success FLUX_SECURITY \
	'flux jobs --recurse-all tries to recurse other user jobs' '
	flux jobs -A --recurse-all > recurse-all.out &&
	test_debug "cat recurse-all.out"  &&
	grep $(cat altid): recurse-all.out
'
test_expect_success 'flux jobs --json works with recursive jobs' '
	flux jobs -A --recursive --json > recursive.json &&
	jq -e ".jobs[] | select(.uri)" < recursive.json &&
	jq -e ".jobs[] | select(.uri) | .jobs[0].id > 0" < recursive.json
'
test_expect_success FLUX_SECURITY 'cancel alternate user job' '
	flux cancel $(cat altid)
'
test_expect_success 'cancel recursive job safely' '
	flux proxy $rid flux cancel --all &&
	flux job wait-event -vt 30 $rid clean
'
test_done
