#!/bin/sh

test_description='Test flux jobs command with --recursive'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

export FLUX_URI_RESOLVE_LOCAL=t
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py --line-buffer -f asciicast"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

#  Start a child instance that immediately exits, so that we can test
#   that `flux jobs -R` doesn't error on child instances that are no
#   longer running.
#
#  Then, start a job hiearachy with 2 child instances, each of which
#   run a sleep job, touch a ready.<id> file, then block waiting for
#   the sleep job to finish.
#
test_expect_success 'start a recursive job' '
	id=$(flux mini submit flux start /bin/true) &&
	rid=$(flux mini submit -n2 \
		flux start \
		flux mini submit --wait --cc=1-2 flux start \
			"flux mini submit sleep inf && \
			 touch ready.\$FLUX_JOB_CC && \
			 flux queue idle") &&
	flux job wait-event $id clean
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
	test_debug "cat recursive.out" &&
	test $(grep -c : recursive-jobid.out) -eq 3
'
test_done
