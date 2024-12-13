#!/bin/sh

test_description='Test flux top command'

. $(dirname $0)/sharness.sh

test_under_flux 4 full

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
testssh="${SHARNESS_TEST_SRCDIR}/scripts/tssh"

export FLUX_URI_RESOLVE_LOCAL=t

# To ensure no raciness in tests below, ensure the job-list
# module knows about submitted jobs in desired states
job_list_wait_state() {
	id=$1
	state=$2
	flux job list-ids --wait-state=$2 $1 > /dev/null
}

# flux-top will redraw panes when a heartbeat is received, which could
# lead to racy output with tests below.  Remove the heartbeat module to
# remove this possibility.
test_expect_success 'set high heartbeat period' '
	flux module remove heartbeat
'
test_expect_success 'flux-top -h prints custom usage' '
	flux top -h 2>usage &&
	grep "Usage:.*TARGET" usage
'
test_expect_success 'flux-top fails on unknown option' '
	test_must_fail flux top --notopt 2>notopt.err &&
	grep "unrecognized option" notopt.err
'
test_expect_success 'flux-top fails if FLUX_URI is set wrong' '
	(FLUX_URI=noturi test_must_fail $runpty -n --stderr=baduri.err flux top) &&
	grep "connecting to Flux" baduri.err
'
test_expect_success 'flux-top fails if job argument is not a valid URI' '
	test_must_fail $runpty -n --stderr=baduri.err flux top baduri &&
	test_debug "cat baduri.err" &&
	grep "failed to resolve" baduri.err
'
test_expect_success 'flux-top fails if job argument is unknown' '
	test_must_fail $runpty -n --stderr=unkjobid.err flux top 12345 &&
	grep "jobid 12345 not found" unkjobid.err
'
test_expect_success 'flux-top --test-exit works with a pty' '
	$runpty flux top --test-exit >/dev/null
'
test_expect_success 'flux-top summary shows no jobs initially' '
	nnodes=$(flux resource list --format="{nnodes}") &&
	ncores=$(flux resource list --format="{ncores}") &&
	$runpty flux top --test-exit --test-exit-dump=nojobs.out > /dev/null &&
	grep "nodes 0/${nnodes}" nojobs.out &&
	grep "cores 0/${ncores}" nojobs.out &&
	grep "0 complete" nojobs.out &&
	grep "0 pending" nojobs.out &&
	grep "0 running" nojobs.out &&
	grep "0 failed" nojobs.out
'
test_expect_success 'flux-top falls back to sched.resource-status' '
	FLUX_RESOURCE_LIST_RPC=foo.status \
		$runpty \
		flux top --test-exit --test-exit-dump=fallback.out >/dev/null &&
	grep "nodes 0/${nnodes}" nojobs.out &&
	grep "cores 0/${ncores}" nojobs.out
'
# Note: jpXCZedGfVQ is the base58 representation of FLUX_JOBID_ANY. We
# grep for this value without f or ƒ in case build environment influences
# presence of one of the other.
#
test_expect_success 'flux-top does not display FLUX_JOBID_ANY jobid in title' '
	test_must_fail grep jpXCZedGfVQ nojobs.out
'
test_expect_success 'run a test job to completion' '
	flux submit --wait -n1 flux start true >jobid &&
	job_list_wait_state $(cat jobid) INACTIVE
'
test_expect_success 'flux-top summary shows one completed job' '
	nnodes=$(flux resource list --format="{nnodes}") &&
	ncores=$(flux resource list --format="{ncores}") &&
	$runpty flux top --test-exit --test-exit-dump=onejob.out >/dev/null &&
	grep "nodes 0/${nnodes}" onejob.out &&
	grep "cores 0/${ncores}" onejob.out &&
	grep "1 complete" onejob.out &&
	grep "0 pending" onejob.out &&
	grep "0 running" onejob.out &&
	grep "0 failed" onejob.out
'
test_expect_success 'flux-top fails if job is not running' '
	test_must_fail \
		$runpty -n --stderr=notrun.err flux top $(cat jobid) &&
	test_debug "cat notrun.err" &&
	grep "jobid $(cat jobid) is not running" notrun.err
'
test_expect_success 'flux-top fails if stdin is not a tty' '
	test_must_fail flux top --test-exit </dev/null 2>notty.err &&
	grep "stdin is not a terminal" notty.err
'
test_expect_success 'flux-top fails if TERM is not supported' '
	test_must_fail $runpty --term=dumb --stderr=dumb.err flux top &&
	grep "terminal does not support required capabilities" dumb.err
'
test_expect_success 'flux-top --color options work' '
	$runpty flux top --color --test-exit >/dev/null &&
	$runpty flux top --color=auto --test-exit >/dev/null &&
	$runpty flux top --color=always --test-exit >/dev/null &&
	$runpty flux top --color=never --test-exit >/dev/null
'
test_expect_success 'flux-top --color fails with bad input' '
	test_must_fail $runpty flux top --color=foo --test-exit >/dev/null
'
test_expect_success 'submit batch script and wait for it to start' '
	cat >batch.sh <<-EOT &&
	#!/bin/sh
	flux submit --wait-event=start sleep 300
	touch job2-has-started
	flux queue drain
	EOT
	chmod +x batch.sh &&
	flux batch -t30m -n1 batch.sh >jobid2 &&
	$waitfile job2-has-started
'
test_expect_success 'flux-top shows 1 job running' '
	nnodes=$(flux resource list --format="{nnodes}") &&
	ncores=$(flux resource list --format="{ncores}") &&
	$runpty flux top --test-exit --test-exit-dump=runningjob.out >/dev/null &&
	grep "nodes 1/${nnodes}" runningjob.out &&
	grep "cores 1/${ncores}" runningjob.out &&
	grep "1 complete" runningjob.out &&
	grep "0 pending" runningjob.out &&
	grep "1 running" runningjob.out &&
	grep "0 failed" runningjob.out &&
	grep "batch.sh" runningjob.out &&
	test_must_fail grep sleep runningjob.out
'
test_expect_success 'flux-top JOBID works' '
	FLUX_SSH=$testssh $runpty flux top --test-exit \
		--test-exit-dump=topjob.out $(cat jobid2) >/dev/null &&
	grep "nodes 1/1" topjob.out &&
	grep "cores 1/1" topjob.out &&
	grep "0 complete" topjob.out &&
	grep "0 pending" topjob.out &&
	grep "1 running" topjob.out &&
	grep "0 failed" topjob.out &&
	grep "sleep" topjob.out &&
	test_must_fail grep "batch.sh" topjob.out
'
test_expect_success 'submit non-batch job and wait for it to start' '
	flux submit -n1 \
		bash -c "touch job3-has-started && sleep 300" >jobid3 &&
	$waitfile job3-has-started &&
	job_list_wait_state $(cat jobid3) RUN
'
test_expect_success 'flux-top shows 2 jobs running' '
	nnodes=$(flux resource list --format="{nnodes}") &&
	ncores=$(flux resource list --format="{ncores}") &&
	$runpty flux top --test-exit --test-exit-dump=tworunningjobs.out >/dev/null &&
	grep "nodes 2/${nnodes}" tworunningjobs.out &&
	grep "cores 2/${ncores}" tworunningjobs.out &&
	grep "1 complete" tworunningjobs.out &&
	grep "0 pending" tworunningjobs.out &&
	grep "2 running" tworunningjobs.out &&
	grep "0 failed" tworunningjobs.out &&
	grep "batch.sh" tworunningjobs.out &&
	grep "bash" tworunningjobs.out
'
test_expect_success 'flux-top JOBID fails when JOBID is not a flux instance' '
	FLUX_SSH=$testssh test_must_fail \
		$runpty --format=asciicast -o notflux.log \
		flux top --test-exit $(cat jobid3) &&
	test_debug "cat notflux.log" &&
	grep "URI not found" notflux.log
'
test_expect_success NO_CHAIN_LINT 'flux-top quits on q keypress' '
	$runpty --quit-char=q --format=asciicast -o keys.log flux top &
	pid=$! &&
	sleep 1 &&
	kill -USR1 $pid &&
	wait $pid
'
# there are two jobs running, the first (listed higher) is the
# non-batch job, the second (listed lower) is the batch job.  So we're
# moving down three times (highlight first job, highlight second job,
# cycle back to first job), moving up (cycle back to second job), then
# hitting enter.
#
# we then hit quit twice to exit
test_expect_success NO_CHAIN_LINT 'flux-top can call itself recursively' '
	SHELL=/bin/sh &&
	flux jobs &&
	flux proxy $(cat jobid2) flux jobs -c1 -no {id} >expected.id &&
	cat <<-EOF >recurse.in &&
	{ "version": 2 }
	[0.5, "i", "j"]
	[1.0, "i", "j"]
	[1.5, "i", "j"]
	[2.0, "i", "k"]
	[2.5, "i", "\n"]
	[3.25, "i", "q"]
	[3.75, "i", "q"]
	EOF
	$runpty -o recurse.log --input=recurse.in flux top &&
	grep -q $(echo $(cat expected.id) | sed "s/ƒ//") recurse.log
'
# note that FLUX_URI_RESOLVE_LOCAL=t is intentionally not set on
# the runpty line below, as we're using a fake ssh
test_expect_success NO_CHAIN_LINT 'flux-top does not exit on recursive failure' '
	cat <<-EOF1 >ssh-fail &&
	#!/bin/sh
	printf "ssh failure\n" >&2
	EOF1
	chmod +x ssh-fail &&
	SHELL=/bin/sh &&
	flux jobs &&
	flux proxy $(cat jobid2) flux jobs -c1 -no {id} >expected.id &&
	cat <<-EOF >recurse-fail.in &&
	{ "version": 2 }
	[0.5, "i", "j"]
	[1.0, "i", "j"]
	[1.5, "i", "j"]
	[2.0, "i", "k"]
	[2.5, "i", "\n"]
	[3.25, "i", "x"]
	[3.75, "i", "q"]
	EOF
	unset FLUX_URI_RESOLVE_LOCAL &&
	FLUX_SSH=$(pwd)/ssh-fail \
	    $runpty -f asciicast -o recurse-fail.log \
	        --input=recurse-fail.in flux top &&
	export FLUX_URI_RESOLVE_LOCAL=t
	grep -qi "error connecting to Flux" recurse-fail.log
'
test_expect_success 'cleanup running jobs' '
	flux cancel $(cat jobid2) $(cat jobid3) &&
	flux job wait-event $(cat jobid2) clean &&
	flux job wait-event $(cat jobid3) clean
'
test_expect_success 'flux-top shows jobs canceled' '
	nnodes=$(flux resource list --format="{nnodes}") &&
	ncores=$(flux resource list --format="{ncores}") &&
	$runpty flux top --test-exit --test-exit-dump=canceledjobs.out >/dev/null &&
	grep "nodes 0/${nnodes}" canceledjobs.out &&
	grep "cores 0/${ncores}" canceledjobs.out &&
	grep "1 complete" canceledjobs.out &&
	grep "0 pending" canceledjobs.out &&
	grep "0 running" canceledjobs.out &&
	grep "2 failed" canceledjobs.out &&
	test_must_fail grep "batch.sh" canceledjobs.out &&
	test_must_fail grep "bash" canceledjobs.out
'
test_expect_success NO_CHAIN_LINT 'flux-top works with FLUX_F58_FORCE_ASCII' '
       FLUX_F58_FORCE_ASCII=1 $runpty -f asciicast -o normalf.log \
			      flux top --test-exit
'
test_expect_success 'configure queues and resource split amongst queues' '
	flux R encode -r 0-3 -p batch:0-1 -p debug:2-3 \
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.debug]
	requires = [ "debug" ]
	EOT
	flux queue start --all &&
	flux module unload sched-simple &&
	flux module reload resource &&
	flux module load sched-simple
'
test_expect_success 'submit a bunch of jobs' '
	flux submit --cc=0-1 --queue=batch bash -c "sleep 300" > batch.ids &&
	flux submit --queue=debug sleep 300 > debug.ids &&
	job_list_wait_state $(head -n1 batch.ids) RUN &&
	job_list_wait_state $(tail -n1 batch.ids) RUN &&
	job_list_wait_state $(cat debug.ids) RUN
'
test_expect_success 'flux-top displays job queues' '
	$runpty -f asciicast -o queue.log flux top --test-exit &&
	grep QUEUE queue.log &&
	grep batch queue.log &&
	grep debug queue.log
'
test_expect_success 'flux-top shows expected data in queues' '
	$runpty flux top --test-exit --test-exit-dump=all.out >/dev/null &&
	grep "nodes 3/4" all.out &&
	grep "cores 3/4" all.out &&
	grep "1 complete" all.out &&
	grep "0 pending" all.out &&
	grep "3 running" all.out &&
	grep "2 failed" all.out
'
test_expect_success 'flux-top fails on invalid queue' '
	test_must_fail flux top --queue=foobar
'
test_expect_success 'flux-top shows expected data in batch queue' '
	$runpty flux top --queue=batch --test-exit \
	    --test-exit-dump=batchq.out >/dev/null &&
	grep "nodes 2/2" batchq.out &&
	grep "cores 2/2" batchq.out &&
	grep "0 complete" batchq.out &&
	grep "0 pending" batchq.out &&
	grep "2 running" batchq.out &&
	grep "0 failed" batchq.out
'
test_expect_success 'flux-top shows expected data in debug queue' '
	$runpty flux top --queue=debug --test-exit \
	    --test-exit-dump=debugq.out >/dev/null &&
	grep "nodes 1/2" debugq.out &&
	grep "cores 1/2" debugq.out &&
	grep "0 complete" debugq.out &&
	grep "0 pending" debugq.out &&
	grep "1 running" debugq.out &&
	grep "0 failed" debugq.out &&
	test $(grep sleep debugq.out | wc -l) -eq 1 &&
	test $(grep debug debugq.out | wc -l) -eq 1
'
test_expect_success 'cancel all jobs' '
	flux cancel --all &&
	flux queue drain
'
test_expect_success 'flux-top shows expected data in queues after cancels' '
	$runpty flux top --test-exit --test-exit-dump=allC.out >/dev/null &&
	grep "nodes 0/4" allC.out &&
	grep "cores 0/4" allC.out &&
	grep "1 complete" allC.out &&
	grep "0 pending" allC.out &&
	grep "0 running" allC.out &&
	grep "5 failed" allC.out
'
test_expect_success 'flux-top shows expected data in batch queue after cancels' '
	$runpty flux top --queue=batch --test-exit \
	    --test-exit-dump=batchqC.out >/dev/null &&
	grep "nodes 0/2" batchqC.out &&
	grep "cores 0/2" batchqC.out &&
	grep "0 complete" batchqC.out &&
	grep "0 pending" batchqC.out &&
	grep "0 running" batchqC.out &&
	grep "2 failed" batchqC.out
'
test_expect_success 'flux-top shows expected data in debug queue after cancels' '
	$runpty flux top --queue=debug --test-exit \
	    --test-exit-dump=debugqC.out >/dev/null &&
	grep "nodes 0/2" debugqC.out &&
	grep "cores 0/2" debugqC.out &&
	grep "0 complete" debugqC.out &&
	grep "0 pending" debugqC.out &&
	grep "0 running" debugqC.out &&
	grep "1 failed" debugqC.out
'
# for interactive test below, job submission order here is important.
# first two jobs are to batch queue, last is to debug queue.  This
# leads to the debug queue job normally being listed first when jobs
# in all queues are listed.  Thus we can test that the jobs specific
# to the batch queue are listed correctly when there is queue
# filtering
test_expect_success 'submit jobs to queues for interactive test' '
	cat >batchQ.sh <<-EOT &&
	#!/bin/sh
	flux submit --wait-event=start sleep 300
	touch job-queue1-has-started
	flux queue drain
	EOT
	chmod +x batchQ.sh &&
	flux batch -t30m -n1 --queue=batch batchQ.sh >jobidQ1 &&
	$waitfile job-queue1-has-started &&
	flux submit -n1 --queue=batch \
		bash -c "touch job-queue2-has-started && sleep 300" >jobidQ2 &&
	$waitfile job-queue2-has-started &&
	flux submit -n1 --queue=debug \
		bash -c "touch job-queue3-has-started && sleep 300" >jobidQ3 &&
	$waitfile job-queue3-has-started
'
# only two batch jobs should be listed in filtered output.  See non-queue
# based equivalent test above for description on what this is doing
# interactively.
test_expect_success NO_CHAIN_LINT 'flux-top can call itself recursively with queue filter' '
	SHELL=/bin/sh &&
	flux jobs &&
	flux proxy $(cat jobidQ1) flux jobs -c1 -no {id} >expectedQ.id &&
	cat <<-EOF >recurseQ.in &&
	{ "version": 2 }
	[0.5, "i", "j"]
	[1.0, "i", "j"]
	[1.5, "i", "j"]
	[2.0, "i", "k"]
	[2.5, "i", "\n"]
	[3.25, "i", "q"]
	[3.75, "i", "q"]
	EOF
	$runpty -o recurseQ.log --input=recurseQ.in flux top --queue=batch &&
	grep -q $(echo $(cat expectedQ.id) | sed "s/ƒ//") recurseQ.log
'
# in order to test that the left/right arrow keys work, we will
# "start" flux-top filtering only jobs in the `batch` queue.  Then
# either the left or right keys should show our job from the debug
# queue.  One direction shows just debug queue, the other direction
# shows "all" queues.
test_expect_success NO_CHAIN_LINT 'flux-top left shows other queue' '
	SHELL=/bin/sh &&
	cat <<-EOF >leftQ.in &&
	{ "version": 2 }
	[0.50, "i", "h"]
	[1.00, "i", "q"]
	EOF
	FLUX_URI_RESOLVE_LOCAL=t $runpty -o leftQ.log --input=leftQ.in \
		flux top --queue=batch &&
	grep -q "debug" leftQ.log
'
test_expect_success NO_CHAIN_LINT 'flux-top right shows other queue' '
	SHELL=/bin/sh &&
	cat <<-EOF >rightQ.in &&
	{ "version": 2 }
	[0.50, "i", "l"]
	[1.00, "i", "q"]
	EOF
	FLUX_URI_RESOLVE_LOCAL=t $runpty -o rightQ.log --input=rightQ.in \
		flux top --queue=batch &&
	grep -q "debug" rightQ.log
'
test_expect_success NO_CHAIN_LINT 'flux-top cycles left and right work' '
	SHELL=/bin/sh &&
	cat <<-EOF >cycleQ.in &&
	{ "version": 2 }
	[0.50, "i", "h"]
	[1.00, "i", "h"]
	[1.50, "i", "h"]
	[2.00, "i", "l"]
	[2.50, "i", "l"]
	[3.00, "i", "l"]
	[3.50, "i", "q"]
	EOF
	FLUX_URI_RESOLVE_LOCAL=t $runpty -o cycleQ.log --input=cycleQ.in \
		flux top --queue=batch
'

test_done
