#!/bin/sh

test_description='Test flux jobs command'

. $(dirname $0)/job-list/job-list-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py --line-buffer -f asciicast"
PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

# submit a whole bunch of jobs for job list testing
#
# - the first loop of job submissions are intended to have some jobs run
#   quickly and complete
# - the second loop of job submissions are intended to eat up all resources
# - the last job submissions are intended to get a create a set of
#   pending jobs, because jobs from the second loop have taken all resources
# - job ids are stored in files in the order we expect them to be listed
#   - pending jobs - by priority (highest first), job id (smaller first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#

export FLUX_PYCLI_LOGLEVEL=10

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

listjobs() {
	${FLUX_BUILD_DIR}/t/job-manager/list-jobs \
	    | $jq .id \
	    | flux job id --to=f58
}

test_expect_success 'configure testing queues' '
	flux config load <<-EOT &&
	[policy]
	jobspec.defaults.system.queue = "defaultqueue"
	[queues.defaultqueue]
	[queues.queue1]
	[queues.queue2]
	EOT
	flux queue start --all
'

test_expect_success 'create helper job submission script' '
	cat >sleepinf.sh <<-EOT &&
	#!/bin/sh
	echo "job started"
	sleep inf
	EOT
	chmod +x sleepinf.sh
'

test_expect_success 'submit jobs for job list testing' '
	#  Create `hostname` and `sleep` jobspec
	#  N.B. Used w/ `flux job submit` for serial job submission
	#  for efficiency (vs serial `flux submit`.
	#
	flux submit --dry-run \
		--queue=queue1 \
		hostname >hostname.json &&
	flux submit --dry-run \
		--time-limit=5m \
		--queue=queue2 \
		sleep 600 > sleeplong.json &&
	#
	#  Submit jobs that will complete
	#
	for i in $(seq 0 3); do
		flux job submit hostname.json >> inactiveids
		fj_wait_event `tail -n 1 inactiveids` clean
	done &&
	#
	#  Currently all inactive ids are "completed"
	#
	tac inactiveids > completed.ids &&
	#
	#  Run a job that will fail, copy its JOBID to both inactive and
	#   failed lists.
	#
	! jobid=`flux submit --wait nosuchcommand` &&
	echo $jobid >> inactiveids &&
	flux job id $jobid > failed_exec.ids &&
	echo $jobid > failedids &&
	#
	#  Run a job that we will end with a signal, copy its JOBID to both inactive and
	#   failed and terminated lists.
	#
	# N.B. sleepinf.sh and wait-event on job data to workaround
	# rare job startup race.  See #5210
	#
	jobid=`flux submit ./sleepinf.sh` &&
	flux job wait-event -W -p guest.output $jobid data &&
	flux job kill $jobid &&
	fj_wait_event $jobid clean &&
	echo $jobid >> inactiveids &&
	flux job id $jobid > terminated.ids &&
	flux job id $jobid >> failedids &&
	#
	#  Run a job that we will end with a user exception, copy its JOBID to both
	#	inactive and failed and exception lists.
	#
	# N.B. sleepinf.sh and wait-event on job data to workaround
	# rare job startup race.  See #5210
	#
	jobid=`flux submit ./sleepinf.sh` &&
	flux job wait-event -W -p guest.output $jobid data &&
	flux job raise --type=myexception --severity=0 -m "myexception" $jobid &&
	fj_wait_event $jobid clean &&
	echo $jobid >> inactiveids &&
	flux job id $jobid > exception.ids &&
	flux job id $jobid >> failedids &&
	#
	#  Run a job that will timeout, copy its JOBID to both inactive and
	#   timeout lists.
	#
	jobid=`flux submit --time-limit=0.5s sleep 30` &&
	echo $jobid >> inactiveids &&
	echo $jobid > timeout.ids &&
	fj_wait_event ${jobid} clean &&
	#
	#  Submit 8 sleep jobs to fill up resources
	#
	for i in $(seq 0 7); do
		flux job submit sleeplong.json >> runids
	done &&
	tac runids > run.ids &&
	#
	#  Submit a set of jobs with non-default urgencies
	#
	for u in 31 25 20 15 10 5 0; do
		flux job submit --urgency=$u sleeplong.json >> sched.ids
	done &&
	listjobs > active.ids &&
	#
	#  Submit a job and cancel it
	#
	# N.B. no need to handle issue #5210 here, the job will not
	# run due to lack of resources.
	#
	jobid=`flux submit --job-name=canceledjob sleep 30` &&
	fj_wait_event $jobid depend &&
	flux cancel -m "mecanceled" $jobid &&
	fj_wait_event $jobid clean &&
	echo $jobid >> inactiveids &&
	echo $jobid > canceled.ids &&
	tac failedids > failed.ids &&
	tac inactiveids > inactive.ids &&
	cat inactive.ids active.ids >> all.ids &&
	#
	#  The job-list module has eventual consistency with the jobs stored in
	#  the job-manager queue.  To ensure no raciness in tests, ensure
	#  jobs above have reached expected states in job-list before continuing.
	#
	flux job list-ids --wait-state=sched $(job_list_state_ids sched) > /dev/null &&
	flux job list-ids --wait-state=run $(job_list_state_ids run) > /dev/null &&
	flux job list-ids --wait-state=inactive $(job_list_state_ids inactive) > /dev/null
'

#
# basic tests
#
test_expect_success 'flux-jobs --no-header works' '
	count=`flux jobs --no-header | wc -l` &&
	test $count -eq $(job_list_state_count active)
'
test_expect_success 'flux-jobs default output works' '
	flux jobs -n > default.out &&
	test $(wc -l < default.out) -eq $(job_list_state_count active) &&
	test $(grep -c "  S " default.out) -eq $(job_list_state_count sched) &&
	test $(grep -c "  R " default.out) -eq $(job_list_state_count run) &&
	test $(grep -c " CD " default.out) -eq 0 &&
	test $(grep -c " CA " default.out) -eq 0 &&
	test $(grep -c "  F " default.out) -eq 0 &&
	test $(grep -c " TO " default.out) -eq 0
'

test_expect_success 'flux-jobs: custom format with numeric spec works' '
	flux jobs --format="{t_run:12.2f}" > format-test.out 2>&1 &&
	test_debug "cat format-test.out" &&
	grep T_RUN format-test.out
'

test_expect_success 'flux-jobs: collapsible fields work' '
	flux jobs -ao "{id.f58:<12} ?:{exception.type:>8}" >nocollapse.out &&
	flux jobs -f running,completed \
		 -o "{id.f58:<12} ?:{exception.type:>8}"   >collapsed.out &&
	test_debug "head -n1 nocollapse.out" &&
	test_debug "head -n1 collapsed.out" &&
	grep EXCEPTION-TYPE nocollapse.out &&
	test_must_fail grep EXCEPTION-TYPE collapsed.out
'
# Note longest name from above should be 'nosuchcommand'
# To ensure field was expanded to this width, ensure NAME header is right
# justified:
test_expect_success 'flux-jobs: expandable fields work' '
	flux jobs -ao "+:{name:>1}" >expanded.out &&
	grep "^ *NAME" expanded.out &&
	grep "^ *sleep" expanded.out &&
	grep "nosuchcommand" expanded.out
'
test_expect_success 'flux-jobs: specified width overrides expandable field' '
	flux jobs -ao "+:{name:>16}" >expanded2.out &&
	test_debug "cat expanded2.out" &&
	grep "^   nosuchcommand" expanded2.out
'
test_expect_success 'flux-jobs: collapsible+expandable fields work' '
	flux jobs -ao "{id.f58:<12} ?+:{exception.type:>1}" >both.out &&
	flux jobs -f running,completed \
		 -o "{id.f58:<12} ?+:{exception.type:>1}" >both-collapsed.out &&
	test_debug "head -n1 both.out" &&
	test_debug "head -n1 both-collapsed.out" &&
	grep EXCEPTION-TYPE both.out &&
	test_must_fail grep EXCEPTION-TYPE both-collapsed.out
'
test_expect_success 'flux-jobs: request indication of truncation works' '
	flux jobs -n -c1 -ano "{id.f58:<5.5+}" | grep + &&
	flux jobs -n -c1 -ano "{id.f58:<5.5h+}" | grep + &&
	flux jobs -n -c1 -ano "{id.f58:<20.20h+}" > notruncate.out &&
	test_must_fail grep + notruncate.out
'

# TODO: need to submit jobs as another user and test -A again
test_expect_success 'flux-jobs -a and -A works' '
	nall=$(job_list_state_count all) &&
	count=`flux jobs --no-header -a | wc -l` &&
	test $count -eq $nall &&
	count=`flux jobs --no-header -a -A | wc -l` &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --since implies -a' '
	nall=$(job_list_state_count all) &&
	count=$(flux jobs --no-header --since=0.0 | wc -l) &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --since with --filter does not imply -a' '
	nfailed=$(job_list_state_count failed) &&
	count=$(flux jobs -n --since=0.0 -f failed | wc -l) &&
	test $count -eq $nfailed
'

test_expect_success 'flux-jobs --since option must specify a time in the past' '
	test_must_fail flux jobs --since=+1m
'

# Print the t_inactive timestamp for the 3rd most recently inactive job
# Then ensure this timestamp limits output to 2 jobs
test_expect_success 'flux-jobs --since works with timestamp' '
	ts=$(flux jobs --filter=inactive -no {t_inactive} | sed -n 3p) &&
	test_debug "flux jobs --filter=inactive --since=${ts}" &&
	count=$(flux jobs -n --filter=inactive --since=${ts} | wc -l) &&
	test $count -eq 2
'

test_expect_success 'flux-jobs --since works with datetime' '
	nall=$(job_list_state_count all) &&
	count=$(flux jobs -n --since="a year ago" | wc -l) &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --since works with fsd offset' '
	nall=$(job_list_state_count all) &&
	count=$(flux jobs -n --since=-8.8h | wc -l) &&
	test $count -eq $nall
'

# -a ignored if --filter specified
test_expect_success 'flux-jobs -a and --filter=pending' '
	count=`flux jobs -n -a --filter=pending 2> filter_a.err | wc -l` &&
	test $count -eq $(job_list_state_count sched) &&
	grep ignoring filter_a.err
'

test_expect_success 'flux-jobs --name works' '
	test_debug "flux jobs -an --name=nosuchcommand" &&
	test $(flux jobs -an --name=nosuchcommand | wc -l) -eq 1 &&
	test_debug "flux jobs -an --name=xxyyzz" &&
	test $(flux jobs -an --name=xxyyzz | wc -l) -eq 0
'

# in job submissions above: completed jobs should be in queue1, running jobs
# in queue2
test_expect_success 'flux-jobs --queue works' '
	test_debug "flux jobs -an --queue=queue1" &&
	test $(flux jobs -an --queue=queue1 | wc -l) -eq $(job_list_state_count completed) &&
	test_debug "flux jobs -an --queue=queue2" &&
	test $(flux jobs -an --queue=queue2 | wc -l) -eq $(job_list_state_count sched run) &&
	test_debug "flux jobs -an --queue=foobar" &&
	test $(flux jobs -an --queue=foobar | wc -l) -eq 0
'
test_expect_success 'flux-jobs --queue accepts multiple queues' '
	test $(flux jobs -anq queue1,queue2 | wc -l) \
		-eq $(job_list_state_count completed sched run)
'

# Recall pending = depend | priority | sched, running = run | cleanup,
#  active = pending | running
test_expect_success 'flux-jobs --filter works (job states)' '
	count=`flux jobs --no-header --filter=depend | wc -l` &&
	test $count -eq 0 &&
	count=`flux jobs --no-header --filter=priority | wc -l` &&
	test $count -eq 0 &&
	count=`flux jobs --no-header --filter=sched | wc -l` &&
	test $count -eq $(job_list_state_count sched) &&
	count=`flux jobs --no-header --filter=pending | wc -l` &&
	test $count -eq $(job_list_state_count sched) &&
	count=`flux jobs --no-header --filter=run | wc -l` &&
	test $count -eq $(job_list_state_count run) &&
	count=`flux jobs --no-header --filter=cleanup | wc -l` &&
	test $count -eq 0 &&
	count=`flux jobs --no-header --filter=running | wc -l` &&
	test $count -eq $(job_list_state_count run) &&
	count=`flux jobs --no-header --filter=inactive | wc -l` &&
	test $count -eq $(job_list_state_count inactive) &&
	count=`flux jobs --no-header --filter=pending,running | wc -l` &&
	test $count -eq $(job_list_state_count sched run) &&
	count=`flux jobs --no-header --filter=sched,run | wc -l` &&
	test $count -eq $(job_list_state_count sched run) &&
	count=`flux jobs --no-header --filter=active | wc -l` &&
	test $count -eq $(job_list_state_count active) &&
	count=`flux jobs --no-header --filter=depend,priority,sched,run,cleanup | wc -l` &&
	test $count -eq $(job_list_state_count active) &&
	count=`flux jobs --no-header --filter=pending,inactive | wc -l` &&
	test $count -eq $(job_list_state_count sched inactive) &&
	count=`flux jobs --no-header --filter=sched,inactive | wc -l` &&
	test $count -eq $(job_list_state_count sched inactive) &&
	count=`flux jobs --no-header --filter=running,inactive | wc -l` &&
	test $count -eq $(job_list_state_count run inactive) &&
	count=`flux jobs --no-header --filter=run,inactive | wc -l` &&
	test $count -eq $(job_list_state_count run inactive) &&
	count=`flux jobs --no-header --filter=pending,running,inactive | wc -l` &&
	test $count -eq $(job_list_state_count all) &&
	count=`flux jobs --no-header --filter=active,inactive | wc -l` &&
	test $count -eq $(job_list_state_count active inactive) &&
	count=`flux jobs --no-header --filter=depend,priority,cleanup | wc -l` &&
	test $count -eq 0
'

test_expect_success 'flux-jobs --filter works (job results)' '
	count=`flux jobs --no-header --filter=completed | wc -l` &&
	test $count -eq $(job_list_state_count completed) &&
	count=`flux jobs --no-header --filter=failed | wc -l` &&
	test $count -eq $(job_list_state_count failed) &&
	count=`flux jobs --no-header --filter=canceled | wc -l` &&
	test $count -eq $(job_list_state_count canceled) &&
	count=`flux jobs --no-header --filter=timeout | wc -l` &&
	test $count -eq $(job_list_state_count timeout) &&
	count=`flux jobs --no-header --filter=completed,failed | wc -l` &&
	test $count -eq $(job_list_state_count completed failed) &&
	count=`flux jobs --no-header --filter=completed,canceled | wc -l` &&
	test $count -eq $(job_list_state_count completed canceled) &&
	count=`flux jobs --no-header --filter=completed,timeout | wc -l` &&
	test $count -eq $(job_list_state_count completed timeout) &&
	count=`flux jobs --no-header --filter=completed,failed,canceled | wc -l` &&
	test $count -eq $(job_list_state_count completed failed canceled) &&
	count=`flux jobs --no-header --filter=completed,failed,timeout | wc -l` &&
	test $count -eq $(job_list_state_count completed failed timeout) &&
	count=`flux jobs --no-header --filter=completed,failed,canceled,timeout | wc -l` &&
	test $count -eq $(job_list_state_count completed failed canceled timeout) &&
	count=`flux jobs --no-header --filter=pending,completed | wc -l` &&
	test $count -eq $(job_list_state_count sched completed) &&
	count=`flux jobs --no-header --filter=pending,failed | wc -l` &&
	test $count -eq $(job_list_state_count sched failed) &&
	count=`flux jobs --no-header --filter=pending,canceled | wc -l` &&
	test $count -eq $(job_list_state_count sched canceled) &&
	count=`flux jobs --no-header --filter=pending,timeout | wc -l` &&
	test $count -eq $(job_list_state_count sched timeout) &&
	count=`flux jobs --no-header --filter=running,completed | wc -l` &&
	test $count -eq $(job_list_state_count run completed) &&
	count=`flux jobs --no-header --filter=running,failed | wc -l` &&
	test $count -eq $(job_list_state_count run failed) &&
	count=`flux jobs --no-header --filter=running,canceled | wc -l` &&
	test $count -eq $(job_list_state_count run canceled) &&
	count=`flux jobs --no-header --filter=running,timeout | wc -l` &&
	test $count -eq $(job_list_state_count run timeout) &&
	count=`flux jobs --no-header --filter=inactive,completed | wc -l` &&
	test $count -eq $(job_list_state_count inactive) &&
	count=`flux jobs --no-header --filter=inactive,failed | wc -l` &&
	test $count -eq $(job_list_state_count inactive) &&
	count=`flux jobs --no-header --filter=inactive,canceled | wc -l` &&
	test $count -eq $(job_list_state_count inactive) &&
	count=`flux jobs --no-header --filter=inactive,timeout | wc -l` &&
	test $count -eq $(job_list_state_count inactive)
'


test_expect_success 'flux-jobs --filter with invalid state fails' '
	test_must_fail flux jobs --filter=foobar 2> invalidstate.err &&
	grep "Invalid filter specified: foobar" invalidstate.err
'

# ensure + prefix works
# increment userid to ensure not current user for test
test_expect_success 'flux-jobs --user=UID works' '
	userid=`id -u` &&
	count=`flux jobs --no-header --user=${userid} | wc -l` &&
	test $count -eq $(job_list_state_count active) &&
	count=`flux jobs --no-header --user="+${userid}" | wc -l` &&
	test $count -eq $(job_list_state_count active) &&
	userid=$((userid+1)) &&
	count=`flux jobs --no-header --user=${userid} | wc -l` &&
	test $count -eq 0
'

test_expect_success 'flux-jobs --user=USERNAME works' '
	username=`whoami` &&
	count=`flux jobs --no-header --user=${username} | wc -l` &&
	test $count -eq $(job_list_state_count active)
'

test_expect_success 'flux-jobs --user with invalid username fails' '
	username="foobarfoobaz" &&
	test_must_fail flux jobs --no-header --user=${username} 2> baduser.out &&
	grep "Invalid user" baduser.out
'

test_expect_success 'flux-jobs --user=all works' '
	count=`flux jobs --no-header --user=all | wc -l` &&
	test $count -eq $(job_list_state_count active)
'

test_expect_success 'flux-jobs --count works' '
	count=`flux jobs --no-header -a --count=0 | wc -l` &&
	test $count -eq $(job_list_state_count all) &&
	count=`flux jobs --no-header -a --count=8 | wc -l` &&
	test $count -eq 8
'

#
# -i, --include tests
#
test_expect_success 'flux-jobs -i, --include works with ranks' '
	for rank in $(flux jobs -ai 0 -no {ranks}); do
		test $rank -eq 0
	done
'
test_expect_success 'flux-jobs -i, --include works with ranks' '
	for rank in $(flux jobs -ai 0,3 -no {ranks}); do
		test $rank -eq 0 -o $rank -eq 3
	done
'
test_expect_success 'flux jobs -i, --include works with hosts' '
	for host in $(flux jobs -ai $(hostname) -no {nodelist}); do
		test $host = $(hostname)
	done
'
test_expect_success 'flux jobs -i, --include fails with bad idset/hostlist' '
	test_must_fail flux jobs -ai "foo["
'

#
# test specific IDs
#

test_expect_success 'flux-jobs specific IDs works' '
	for state in sched run inactive; do
		ids=$(job_list_state_ids $state) &&
		expected=$(job_list_state_count $state) &&
		count=$(flux jobs -n ${ids} | wc -l) &&
		test_debug "echo Got ${count} of ${expected} ids in ${state} state" &&
		test $count -eq $expected
	done
'

test_expect_success 'flux jobs can take specific IDs in any form' '
	id=$(head -1 run.ids) &&
	for f in f58 hex dothex kvs words; do
		flux job id --to=${f} ${id}
	done > ids.specific.list &&
	flux jobs -no {id} $(cat ids.specific.list) > ids.specific.out &&
	for i in $(seq 1 5); do echo $id >>ids.specific.expected; done &&
	test_cmp ids.specific.expected ids.specific.out
'

test_expect_success 'flux-jobs error on unknown IDs' '
	flux jobs --no-header 0 1 2 2> ids.err &&
	count=`grep -i unknown ids.err | wc -l` &&
	test $count -eq 3
'

test_expect_success 'flux-jobs errors with illegal IDs' '
	test_must_fail flux jobs --no-header IllegalID 2> illegal_ids.err &&
	grep "invalid JobID value" illegal_ids.err
'

test_expect_success 'flux-jobs good and bad IDs works' '
	ids=$(job_list_state_ids sched) &&
	flux jobs --no-header ${ids} 0 1 2 > ids.out 2> ids.err &&
	count=`wc -l < ids.out` &&
	test $count -eq $(job_list_state_count sched) &&
	count=`grep -i unknown ids.err | wc -l` &&
	test $count -eq 3
'

test_expect_success 'flux-jobs outputs warning on invalid options' '
	ids=$(job_list_state_ids sched) &&
	flux jobs --no-header -A ${ids} > warn.out 2> warn.err &&
	grep WARNING warn.err
'

#
# format tests
#

test_expect_success 'flux-jobs --format={id} works' '
	flux jobs --no-header --filter=pending --format="{id}" > idsP.out &&
	test_cmp idsP.out sched.ids &&
	flux jobs --no-header --filter=running --format="{id}" > idsR.out &&
	test_cmp idsR.out run.ids &&
	flux jobs --no-header --filter=inactive --format="{id}" > idsI.out &&
	test_cmp idsI.out inactive.ids
'

test_expect_success 'flux-jobs --format={id.f58},{id.f58plain},{id.hex},{id.dothex},{id.words} works' '
	flux jobs -ano {id.dec},{id.f58},{id.f58plain},{id.hex},{id.kvs},{id.dothex},{id.words} \
	    | sort -n > ids.XX.out &&
	for id in $(cat all.ids); do
		printf "%s,%s,%s,%s,%s,%s,%s\n" \
		       $(flux job id --to=dec $id) \
		       $(flux job id --to=f58 $id) \
		       $(flux job id --to=f58plain $id) \
		       $(flux job id --to=hex $id) \
		       $(flux job id --to=kvs $id) \
		       $(flux job id --to=dothex $id) \
		       $(flux job id --to=words $id)
	done | sort -n > ids.XX.expected &&
	test_cmp ids.XX.expected ids.XX.out
'

test_expect_success 'flux-jobs --format={userid},{username} works' '
	flux jobs --no-header -a --format="{userid},{username}" > user.out &&
	id=`id -u` &&
	name=`whoami` &&
	for i in `seq 1 $(job_list_state_count all)`; do
		echo "${id},${name}" >> user.exp
	done &&
	test_cmp user.out user.exp
'

test_expect_success 'flux-jobs --format={urgency},{priority} works' '
	flux jobs --no-header -a --format="{urgency},{priority}" > urgency_priority.out &&
	echo 31,4294967295 > urgency_priority.exp &&
	echo 25,25 >> urgency_priority.exp &&
	echo 20,20 >> urgency_priority.exp &&
	echo 15,15 >> urgency_priority.exp &&
	echo 10,10 >> urgency_priority.exp &&
	echo 5,5 >> urgency_priority.exp &&
	echo 0,0 >> urgency_priority.exp &&
	for i in `seq 1 $(job_list_state_count run)`; do
		echo "16,16" >> urgency_priority.exp
	done &&
	for i in `seq 1 $(job_list_state_count inactive)`; do
		echo "16,16" >> urgency_priority.exp
	done &&
	test_cmp urgency_priority.out urgency_priority.exp
'

test_expect_success 'flux-jobs --format={contextual_info} shows held job' '
	flux jobs -no {urgency}:{contextual_info} | grep 0:held
'

# There is no simple way to create a job with urgency > 0 and priority == 0,
# so test priority-hold using --from-stdin:
test_expect_success 'flux-jobs {contextual_info} shows priority-hold job' '
	echo "{\"id\":195823665152,\"state\":8,\"priority\":0,\"urgency\":16}" \
		| flux jobs --from-stdin -no {priority}:{contextual_info} \
		| grep 0:priority-hold
'

test_expect_success 'flux-jobs --format={state},{state_single} works' '
	flux jobs --filter=pending -c1 -no "{state},{state_single}" > stateP.out &&
	test "$(cat stateP.out)" = "SCHED,S" &&
	flux jobs --filter=running -c1 -no "{state},{state_single}" > stateR.out &&
	test "$(cat stateR.out)" = "RUN,R" &&
	flux jobs --filter=inactive -c1 -no "{state},{state_single}" > stateI.out &&
	test "$(cat stateI.out)" = "INACTIVE,I"
'

# grepping for specific unicode chars is hard, so we just grep to make
# sure a unicode character was output.  Be sure to disable color too,
# since there can be unicode in there.
test_expect_success 'flux-jobs --format={state_emoji} works' '
	$runpty flux jobs --filter=pending -c1 --color=never -no "{state_emoji}" > stateE_P.out &&
	grep "\\u" stateE_P.out &&
	$runpty flux jobs --filter=running -c1 --color=never -no "{state_emoji}" > stateE_R.out &&
	grep "\\u" stateE_R.out &&
	$runpty flux jobs --filter=inactive -c1 --color=never -no "{state_emoji}" > stateE_I.out &&
	grep "\\u" stateE_I.out
'

test_expect_success 'flux-jobs --format={name} works' '
	flux jobs --filter=pending,running -no "{name}" > jobnamePR.out &&
	for i in `seq 1 $(job_list_state_count run sched)`; do
		echo "sleep" >> jobnamePR.exp
	done &&
	test_cmp jobnamePR.out jobnamePR.exp &&
	flux jobs --filter=inactive -no "{name}" > jobnameI.out &&
	echo "canceledjob" >> jobnameI.exp &&
	echo "sleep" >> jobnameI.exp &&
	echo "sleepinf.sh" >> jobnameI.exp &&
	echo "sleepinf.sh" >> jobnameI.exp &&
	echo "nosuchcommand" >> jobnameI.exp &&
	count=$(($(job_list_state_count inactive) - 5)) &&
	for i in `seq 1 $count`; do
		echo "hostname" >> jobnameI.exp
	done &&
	test_cmp jobnameI.out jobnameI.exp
'

test_expect_success 'flux-jobs --format={cwd} works' '
	pwd=$(pwd) &&
	flux jobs -a -no "{cwd}" > jobcwd.out &&
	for i in `seq 1 $(job_list_state_count all)`; do
		echo "${pwd}" >> jobcwd.exp
	done &&
	test_cmp jobcwd.out jobcwd.exp
'

# in job submissions above: completed jobs should be in queue1, running jobs
# in queue2, and the rest in defaultqueue
test_expect_success 'flux-jobs --format={queue} works' '
	flux jobs --filter=completed -no "{queue}" > jobqueueCD.out &&
	for i in `seq 1 $(job_list_state_count completed)`; do
		echo "queue1" >> jobqueueCD.exp
	done &&
	test_cmp jobqueueCD.out jobqueueCD.exp &&
	flux jobs --filter=running -no "{queue}" > jobqueueR.out &&
	for i in `seq 1 $(job_list_state_count run)`; do
		echo "queue2" >> jobqueueR.exp
	done &&
	test_cmp jobqueueR.out jobqueueR.exp &&
	flux jobs --filter=failed -no "{queue}" > jobqueueF.out &&
	for i in `seq 1 $(job_list_state_count failed)`; do
		echo "defaultqueue" >> jobqueueF.exp
	done &&
	test_cmp jobqueueF.out jobqueueF.exp
'

test_expect_success 'flux-jobs --format={ntasks} works' '
	flux jobs -a -no "{ntasks}" > ntasks.out &&
	for i in `seq 1 $(job_list_state_count all)`; do
		echo "1" >> ntasks.exp
	done &&
	test_cmp ntasks.exp ntasks.out
'

test_expect_success 'flux-jobs --format={ncores} works' '
	flux jobs -a -no "{ncores}" > ncores.out &&
	for i in `seq 1 $(job_list_state_count all)`; do
		echo "1" >> ncores.exp
	done &&
	test_cmp ncores.exp ncores.out
'

test_expect_success 'flux-jobs --format={duration},{duration:h},{duration!F},{duration!H},{duration!F:h},{duration!H:h} works' '
	fmt="{duration},{duration:h},{duration!F},{duration!H},{duration!F:h},{duration!H:h}" &&
	flux jobs --filter=pending,running -no "${fmt}" > durationPR.out &&
	for i in `seq 1 $(job_list_state_count sched run)`; do
		echo "300.0,300.0,5m,0:05:00,5m,0:05:00" >> durationPR.exp
	done &&
	test_cmp durationPR.exp durationPR.out &&
	flux jobs --filter=completed -no "${fmt}" > durationCD.out &&
	for i in `seq 1 $(job_list_state_count completed)`;
		do echo "0.0,-,0s,0:00:00,-,-" >> durationCD.exp
	done &&
	test_cmp durationCD.exp durationCD.out
'

test_expect_success 'flux-jobs --format={nnodes},{nnodes:h} works' '
	flux jobs --filter=pending -no "{nnodes},{nnodes:h}" > nodecountP.out &&
	for i in `seq 1 $(job_list_state_count sched)`; do
		echo ",-" >> nodecountP.exp
	done &&
	test_cmp nodecountP.exp nodecountP.out &&
	flux jobs --filter=running -no "{nnodes},{nnodes:h}" > nodecountR.out &&
	for i in `seq 1 $(job_list_state_count run)`; do
		echo "1,1" >> nodecountR.exp
	done &&
	test_cmp nodecountR.exp nodecountR.out &&
	flux jobs --filter=inactive -no "{nnodes},{nnodes:h}" > nodecountI.out &&
	echo ",-" > nodecountI.exp &&
	for i in `seq 1 $(job_list_state_count completed failed timeout)`;
		do echo "1,1" >> nodecountI.exp
	done &&
	test_cmp nodecountI.exp nodecountI.out
'

test_expect_success 'flux-jobs --format={runtime:0.3f} works' '
	flux jobs --filter=pending -no "{runtime:0.3f}" > runtime-dotP.out &&
	for i in `seq 1 $(job_list_state_count sched)`;
		do echo "0.000" >> runtime-dotP.exp;
	done &&
	test_cmp runtime-dotP.out runtime-dotP.exp &&
	flux jobs --filter=running,inactive -no "{runtime:0.3f}" > runtime-dotRI.out &&
	[ "$(grep -E "\.[0-9]{3}" runtime-dotRI.out | wc -l)" = "17" ]
'

test_expect_success 'flux-jobs --format={contextual_time} works' '
	flux jobs --filter=pending -c1 -no "{contextual_time:h}" \
		>ctx_timeP.out &&
	flux jobs --filter=running -c1 -no "{contextual_time:h}" \
		>ctx_timeR.out &&
	flux jobs --filter=completed -c1 -no "{contextual_time:0.3f}" \
		>ctx_timeCD.out &&
	echo "300.0" >duration.expected &&
	test_cmp duration.expected ctx_timeP.out &&
	test_must_fail test_cmp duration.expected ctx_timeR.out &&
	test_must_fail test_cmp duration.expected ctx_timeCD.out &&
	grep -E "\.[0-9]{3}" ctx_timeCD.out
'

test_expect_success 'flux-jobs emits useful error on invalid format' '
	test_expect_code 1 flux jobs --format="{runtime" >invalid.out 2>&1 &&
	test_debug "cat invalid.out" &&
	grep "Error in user format" invalid.out
'

test_expect_success 'flux-jobs emits useful error on invalid format field' '
	test_expect_code 1 flux jobs --format="{footime}" >invalid-field.out 2>&1 &&
	test_debug "cat invalid-field.out" &&
	grep "Unknown format field" invalid-field.out
'

test_expect_success 'flux-jobs emits useful error on invalid format specifier' '
	test_expect_code 1 flux jobs --format="{runtime:garbage}" >invalid-spec.out 2>&1 &&
	test_debug "cat invalid-spec.out" &&
	grep "Invalid format specifier" invalid-spec.out
'

test_expect_success 'flux-jobs --format={ranks},{ranks:h} works' '
	flux jobs --filter=pending -no "{ranks},{ranks:h}" > ranksP.out &&
	for i in `seq 1 $(job_list_state_count sched)`; do
		echo ",-" >> ranksP.exp
	done &&
	test_cmp ranksP.out ranksP.exp &&
	#
	#  Ensure 1 running job lists a rank, we cannot know the exact
	#  ranks on which every job ran.
	#
	flux jobs --filter=running -no "{ranks},{ranks:h}" > ranksR.out &&
	test_debug "cat ranksR.out" &&
	test "$(sort -n ranksR.out | head -1)" = "0,0" &&
	flux jobs -no "{ranks},{ranks:h}" $(job_list_state_ids completed) > ranksCD.out &&
	test_debug "cat ranksCD.out" &&
	test "$(sort -n ranksCD.out | head -1)" = "0,0" &&
	flux jobs -no "{ranks},{ranks:h}" $(job_list_state_ids canceled) > ranksCA.out &&
	test_debug "cat ranksCA.out" &&
	test "$(sort -n ranksCA.out | head -1)" = ",-" &&
	flux jobs -no "{ranks},{ranks:h}" $(job_list_state_ids timeout) > ranksTO.out &&
	test_debug "cat ranksTO.out" &&
	test "$(sort -n ranksTO.out | head -1)" = "0,0"
'

test_expect_success 'flux-jobs --format={nodelist},{nodelist:h} works' '
	flux jobs --filter=pending -no "{nodelist},{nodelist:h}" > nodelistP.out &&
	for i in `seq 1 $(job_list_state_count sched)`; do
		echo ",-" >> nodelistP.exp
	done &&
	test_cmp nodelistP.out nodelistP.exp &&
	flux jobs --filter=running -no "{nodelist},{nodelist:h}" > nodelistR.out &&
	for id in $(job_list_state_ids run); do
		nodes=`flux job info ${id} R | flux R decode --nodelist`
		echo "${nodes},${nodes}" >> nodelistR.exp
	done &&
	test_debug "cat nodelistR.out" &&
	test_cmp nodelistR.out nodelistR.exp &&
	flux jobs -no "{nodelist},{nodelist:h}" $(job_list_state_ids completed timeout) > nodelistCDTO.out &&
	for id in $(job_list_state_ids completed timeout); do
		nodes=`flux job info ${id} R | flux R decode --nodelist`
		echo "${nodes},${nodes}" >> nodelistCDTO.exp
	done &&
	test_debug "cat nodelistCDTO.out" &&
	test_cmp nodelistCDTO.out nodelistCDTO.exp &&
	flux jobs -no "{nodelist},{nodelist:h}" $(job_list_state_ids canceled) > nodelistCA.out &&
	test_debug "cat nodelistCA.out" &&
	echo ",-" > nodelistCA.exp &&
	test_cmp nodelistCA.out nodelistCA.exp
'

# test just make sure numbers are zero or non-zero given state of job
test_expect_success 'flux-jobs --format={t_submit/depend} works' '
	flux jobs -ano "{t_submit},{t_depend}" >t_SD.out &&
	count=`cut -d, -f1 t_SD.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count all) &&
	count=`cut -d, -f2 t_SD.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count all)
'
test_expect_success 'flux-jobs --format={t_run} works' '
	flux jobs --filter=pending -no "{t_run},{t_run:h}" > t_runP.out &&
	flux jobs --filter=running -no "{t_run}" > t_runR.out &&
	flux jobs --filter=inactive -no "{t_run}" > t_runI.out &&
	count=`cut -d, -f1 t_runP.out | grep "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count sched) &&
	count=`cut -d, -f2 t_runP.out | grep "^-$" | wc -l` &&
	test $count -eq $(job_list_state_count sched) &&
	count=`cat t_runR.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count run) &&
	cat t_runI.out &&
	count=`head -n 1 t_runI.out | grep "^0.0$" | wc -l` &&
	test $count -eq 1 &&
	count=`tail -n $(job_list_state_count completed timeout) t_runI.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count completed timeout)
'
test_expect_success 'flux jobs --format={t_cleanup/{in}active} works' '
	flux jobs --filter=pending,running -no "{t_cleanup},{t_cleanup:h},{t_inactive},{t_inactive:h}" > t_cleanupPR.out &&
	flux jobs --filter=inactive -no "{t_cleanup},{t_inactive}" > t_cleanupI.out &&
	count=`cut -d, -f1 t_cleanupPR.out | grep "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count sched run) &&
	count=`cut -d, -f2 t_cleanupPR.out | grep "^-$" | wc -l` &&
	test $count -eq $(job_list_state_count sched run) &&
	count=`cut -d, -f1 t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count inactive) &&
	count=`cut -d, -f3 t_cleanupPR.out | grep "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count sched run) &&
	count=`cut -d, -f4 t_cleanupPR.out | grep "^-$" | wc -l` &&
	test $count -eq $(job_list_state_count sched run) &&
	count=`cut -d, -f2 t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(job_list_state_count inactive)
'

test_expect_success 'flux-jobs --format={runtime},{runtime!F},{runtime!H},{runtime!F:h},{runtime!H:h} works' '
	fmt="{runtime},{runtime!F},{runtime!H},{runtime!F:h},{runtime!H:h}" &&
	flux jobs --filter=pending -no "${fmt}" > runtimeP.out &&
	for i in `seq 1 $(job_list_state_count sched)`; do
		echo "0.0,0s,0:00:00,-,-" >> runtimeP.exp
	done &&
	test_cmp runtimeP.out runtimeP.exp &&
	runcount=$(job_list_state_count run) &&
	flux jobs --filter=running -no "${fmt}" > runtimeR.out &&
	i=1 &&
	for nomatch in 0.0 0s 0:00:00 - -; do
		name=$(echo $fmt | cut -d, -f${i}) &&
		count=$(cut -d, -f${i} runtimeR.out | grep -v "^${nomatch}$" | wc -l) &&
		test_debug "echo $name field $i ${nomatch} $count/$runcount times" &&
		test $count -eq $runcount &&
		i=$((i+1))
	done &&
	expected=$(job_list_state_count completed timeout) &&
	flux jobs -no "$fmt" $(job_list_state_ids completed timeout) >runtimeCDTO.out &&
	i=1 &&
	for nomatch in 0.0 0s 0:00:00 - -; do
		name=$(echo $fmt | cut -d, -f${i}) &&
		count=$(cut -d, -f${i} runtimeCDTO.out |grep -v "^${nomatch}$" | wc -l) &&
		test_debug "echo $name: field $i: ${nomatch} $count/$expected times" &&
		test $count -eq $expected &&
		i=$((i+1))
	done
'

test_expect_success 'flux-jobs --format={success},{success:h} works' '
	flux jobs --filter=pending,running -no "{success},{success:h}" > successPR.out &&
	for i in `seq 1 $(job_list_state_count sched run)`; do
		echo ",-" >> successPR.exp;
	done &&
	test_cmp successPR.out successPR.exp &&
	flux jobs --filter=inactive -no "{success},{success:h}" > successI.out &&
	test $(grep -c False,False successI.out) -eq $(job_list_state_count failed canceled timeout) &&
	test $(grep -c True,True successI.out) -eq $(job_list_state_count completed)
'

test_expect_success 'flux-jobs --format={exception.*},{exception.*:h} works' '
	fmt="{exception.occurred},{exception.occurred:h}" &&
	fmt="${fmt},{exception.severity},{exception.severity:h}" &&
	fmt="${fmt},{exception.type},{exception.type:h}" &&
	fmt="${fmt},{exception.note},{exception.note:h}" &&
	flux jobs --filter=pending,running -no "$fmt" > exceptionPR.out &&
	count=$(grep -c "^,-,,-,,-,,-$" exceptionPR.out) &&
	test $count -eq $(job_list_state_count sched run) &&
	flux jobs --filter=inactive -no "$fmt" > exceptionI.out &&
	count=$(grep -c "^True,True,0,0,cancel,cancel,mecanceled,mecanceled$" exceptionI.out) &&
	test $count -eq $(job_list_state_count canceled) &&
	count=$(grep -c "^True,True,0,0,myexception,myexception,myexception,myexception$" exceptionI.out) &&
	test $count -eq $(job_list_state_count exception) &&
	count=$(grep -c "^True,True,0,0,exec,exec,.*No such file.*" exceptionI.out) &&
	test $count -eq $(job_list_state_count failed_exec) &&
	count=$(grep -c "^True,True,0,0,timeout,timeout,.*expired.*" exceptionI.out) &&
	test $count -eq $(job_list_state_count timeout) &&
	count=$(grep -c "^False,False,,-,,-,," exceptionI.out) &&
	test $count -eq $(job_list_state_count completed terminated)
'


test_expect_success 'flux-jobs --format={result},{result:h},{result_abbrev},{result_abbrev:h} works' '
	fmt="{result},{result:h},{result_abbrev},{result_abbrev:h}" &&
	flux jobs --filter=pending,running -no "$fmt" > resultPR.out &&
	count=$(grep -c "^,-,,-$" resultPR.out) &&
	test_debug "echo checking sched+run got $count" &&
	test $count -eq $(job_list_state_count sched run) &&
	flux jobs  --filter=inactive -no "$fmt" > resultI.out &&
	count=$(grep -c "CANCELED,CANCELED,CA,CA" resultI.out) &&
	test_debug "echo checking canceled got $count" &&
	test $count -eq $(job_list_state_count canceled) &&
	count=$(grep -c "FAILED,FAILED,F,F" resultI.out) &&
	test_debug "echo checking failed got $count" &&
	test $count -eq $(job_list_state_count failed) &&
	count=$(grep -c "TIMEOUT,TIMEOUT,TO,TO" resultI.out) &&
	test_debug "echo checking timeout got $count" &&
	test $count -eq $(job_list_state_count timeout) &&
	count=$(grep -c "COMPLETED,COMPLETED,CD,CD" resultI.out) &&
	test_debug "echo checking completed got $count" &&
	test $count -eq $(job_list_state_count completed)
'

# grepping for specific unicode chars is hard, so we just grep to make
# sure a unicode character was output.  Be sure to disable color too,
# since there can be unicode in there.
test_expect_success 'flux-jobs --format={result_emoji} works' '
	$runpty flux jobs --filter=pending -c1 --color=never -no "{result_emoji}" > resultE_P.out &&
	grep "\\u" -v resultE_P.out &&
	$runpty flux jobs --filter=running -c1 --color=never -no "{result_emoji}" > resultE_R.out &&
	grep "\\u" -v resultE_R.out &&
	$runpty flux jobs --filter=completed -c1 --color=never -no "{result_emoji}" > resultE_CD.out &&
	grep "\\u" resultE_CD.out &&
	$runpty flux jobs --filter=failed -c1 --color=never -no "{result_emoji}" > resultE_F.out &&
	grep "\\u" resultE_F.out &&
	$runpty flux jobs --filter=timeout -c1 --color=never -no "{result_emoji}" > resultE_CA.out &&
	grep "\\u" resultE_CA.out &&
	$runpty flux jobs --filter=canceled -c1 --color=never -no "{result_emoji}" > resultE_TO.out &&
	grep "\\u" resultE_TO.out
'

test_expect_success 'flux-jobs --format={status},{status_abbrev} works' '
	flux jobs --filter=sched    -no "{status},{status_abbrev}" > statusS.out &&
	flux jobs --filter=run      -no "{status},{status_abbrev}" > statusR.out &&
	flux jobs --filter=inactive -no "{status},{status_abbrev}" > statusI.out &&
	count=$(grep -c "SCHED,S" statusS.out) &&
	test $count -eq $(job_list_state_count sched) &&
	count=$(grep -c "RUN,R" statusR.out) &&
	test $count -eq $(job_list_state_count run) &&
	count=$(grep -c "CANCELED,CA" statusI.out) &&
	test $count -eq $(job_list_state_count canceled) &&
	count=$(grep -c "FAILED,F" statusI.out) &&
	test $count -eq $(job_list_state_count failed) &&
	count=$(grep -c "TIMEOUT,TO" statusI.out) &&
	test $count -eq $(job_list_state_count timeout) &&
	count=$(grep -c "COMPLETED,CD" statusI.out) &&
	test $count -eq $(job_list_state_count completed)
'

# grepping for specific unicode chars is hard, so we just grep to make
# sure a unicode character was output.  Be sure to disable color too,
# since there can be unicode in there.
test_expect_success 'flux-jobs --format={status_emoji} works' '
	$runpty flux jobs --filter=pending -c1 --color=never -no "{status_emoji}" > statusE_P.out &&
	grep "\\u" statusE_P.out &&
	$runpty flux jobs --filter=running -c1 --color=never -no "{status_emoji}" > statusE_R.out &&
	grep "\\u" statusE_R.out &&
	$runpty flux jobs --filter=completed -c1 --color=never -no "{status_emoji}" > statusE_CD.out &&
	grep "\\u" statusE_CD.out &&
	$runpty flux jobs --filter=failed -c1 --color=never -no "{status_emoji}" > statusE_F.out &&
	grep "\\u" statusE_F.out &&
	$runpty flux jobs --filter=timeout -c1 --color=never -no "{status_emoji}" > statusE_CA.out &&
	grep "\\u" statusE_CA.out &&
	$runpty flux jobs --filter=canceled -c1 --color=never -no "{status_emoji}" > statusE_TO.out &&
	grep "\\u" statusE_TO.out
'

test_expect_success 'flux-jobs --format={waitstatus},{returncode}' '
	FORMAT="{waitstatus:h},{returncode:h}" &&
	flux jobs --filter=pending,running -no "$FORMAT" > returncodePR.out &&
	flux jobs --filter=inactive -no "$FORMAT" > returncodeI.out &&
	test_debug "echo active:; cat returncodePR.out" &&
	test_debug "echo inactive:; cat returncodeI.out" &&
	countPR=$(grep -c "^-,-$" returncodePR.out) &&
	test_debug "echo active got $countPR, want $(job_list_state_count sched run)" &&
	test $countPR -eq $(job_list_state_count sched run) &&
	count=$(grep -c "^32512,127$" returncodeI.out) &&
	test_debug "echo exit 127 got $count, want $(job_list_state_count failed_exec)" &&
	test $count -eq $(job_list_state_count failed_exec) &&
	count=$(grep -c "^36608,143$" returncodeI.out) &&
	test_debug "echo exit 143 got $count, want $(job_list_state_count terminated exception)" &&
	test $count -eq $(job_list_state_count terminated exception) &&
	count=$(grep -c "^36352,142$" returncodeI.out) &&
	test_debug "echo exit 142 got $count, want $(job_list_state_count timeout)" &&
	test $count -eq $(job_list_state_count timeout) &&
	count=$(grep -c "^0,0$" returncodeI.out) &&
	test_debug "echo complete got $count, want $(job_list_state_count completed)" &&
	test $count -eq $(job_list_state_count completed) &&
	count=$(grep -c "^-,-128$" returncodeI.out) &&
	test_debug "echo canceled got $count, want $(job_list_state_count canceled)" &&
	test $count -eq $(job_list_state_count canceled)
'

test_expect_success 'flux-jobs --format={inactive_reason}' '
	FORMAT="{inactive_reason:h}" &&
	flux jobs --filter=pending,running -no "$FORMAT" > inactivereasonPR.out &&
	count=$(grep -c "^-$" inactivereasonPR.out) &&
	test_debug "echo empty got $count, want $(job_list_state_count active)" &&
	test $count -eq $(job_list_state_count active) &&
	flux jobs --filter=inactive -no "$FORMAT" > inactivereasonI.out &&
	count=$(grep -c "^command not found$" inactivereasonI.out) &&
	test_debug "echo command not found got $count, want $(job_list_state_count failed_exec)" &&
	test $count -eq $(job_list_state_count failed_exec) &&
	count=$(grep -c "^Terminated$" inactivereasonI.out) &&
	test_debug "echo Terminated got $count, want $(job_list_state_count terminated)" &&
	test $count -eq $(job_list_state_count terminated) &&
	count=$(grep -c "^Exception: type=myexception note=myexception$" inactivereasonI.out) &&
	test_debug "echo exception got $count, want $(job_list_state_count exception)" &&
	test $count -eq $(job_list_state_count exception) &&
	count=$(grep -c "^Timeout$" inactivereasonI.out) &&
	test_debug "echo Timeout got $count, want $(job_list_state_count timeout)" &&
	test $count -eq $(job_list_state_count timeout) &&
	count=$(grep -c "^Exit 0$" inactivereasonI.out) &&
	test_debug "echo Exit 0 got $count, want $(job_list_state_count completed)" &&
	test $count -eq $(job_list_state_count completed) &&
	count=$(grep -c "^Canceled: mecanceled$" inactivereasonI.out) &&
	test_debug "echo canceled got $count, want $(job_list_state_count canceled)" &&
	test $count -eq $(job_list_state_count canceled)
'

test_expect_success 'flux-jobs --format={expiration},{t_remaining} works' '
	expiration=$(flux jobs -f running -c1 -no "{expiration:.0f}") &&
	t_remaining=$(flux jobs -f running -c1 -no "{t_remaining:.0f}") &&
	t_now=$(date +%s) &&
	test_debug "echo expiration=$expiration, t_remaining=$t_remaining" &&
	test $t_remaining -gt 0 &&
	test $expiration  -gt $t_now
'
test_expect_success 'flux-jobs --format={expiration!D},{t_remaining!F} works' '
	exp=$(($(date +%s) + 600)) &&
	cat <<-EOF >expiration.in &&
{"id": 1447588528128, "state": 8,  "expiration": ${exp}.0 }
	EOF
	expiration=$(cat expiration.in | \
	flux jobs --from-stdin -no "{expiration!D}") &&
	t_remaining=$(cat expiration.in | \
		     flux jobs --from-stdin -no "{t_remaining!F}") &&
	test_debug "echo expiration=$expiration, t_remaining=$t_remaining" &&
	test "${expiration}" = "$(date --date=@${exp} +%FT%T)" &&
	test_debug "echo expiration OK" &&
	echo ${t_remaining} | grep "^[0-9.][0-9.]*[smdh]"
'
test_expect_success 'flux-jobs --format={expiration!d:%FT%T},{t_remaining!H} works' '
	exp=$(($(date +%s) + 600)) &&
	cat <<-EOF >expiration.in &&
{"id": 1447588528128, "state": 8,  "expiration": ${exp}.0 }
	EOF
	expiration=$(cat expiration.in | \
	flux jobs --from-stdin -no "{expiration!d:%FT%T}") &&
	t_remaining=$(cat expiration.in | \
		     flux jobs --from-stdin -no "{t_remaining!H}") &&
	test_debug "echo expiration=$expiration, t_remaining=$t_remaining" &&
	test "${expiration}" = "$(date --date=@${exp} +%FT%T)" &&
	test_debug "echo expiration OK" &&
	echo ${t_remaining} | grep "[0-9]:[0-9][0-9]:[0-9][0-9]"
'
test_expect_success 'flux-jobs --format={expiration!d:%FT%T::>20.20} works' '
	cat expiration.in | \
	    flux jobs --from-stdin -o "{expiration!d:%b%d %R::>20.20}" \
	    >exp-fmt.out &&
	cat expiration.in | \
	    flux jobs --from-stdin -o "{expiration!d:%b%d %R::>20.20h}" \
	    >exp-fmth.out &&
	test_debug "cat exp-fmt.out" &&
	grep "          EXPIRATION" exp-fmt.out &&
	grep "         $(date --date=@${exp} +%b%d\ %R)" exp-fmt.out &&
	test_debug "cat exp-fmth.out" &&
	grep "          EXPIRATION" exp-fmth.out &&
	grep "         $(date --date=@${exp} +%b%d\ %R)" exp-fmth.out
'
test_expect_success 'flux-jobs --format={expiration!d} works' '
	cat expiration.in | flux jobs --from-stdin -o "{expiration!d}" \
	    >exp-fmtd.out &&
	grep "$(date --date=@${exp} +%FT%T)" exp-fmtd.out
'
test_expect_success 'flux-jobs --format={expiration!d:%FT%T::=^20} works' '
	cat expiration.in | \
	    flux jobs --from-stdin -o "{expiration!d:%b%d %R::=^20}" \
	    >exp-fmt.out &&
	test_debug "cat exp-fmt.out" &&
	grep "     EXPIRATION     " exp-fmt.out &&
	grep "====$(date --date=@${exp} +%b%d\ %R)====" exp-fmt.out
'

test_expect_success 'flux-jobs --format={expiration!D:h},{t_remaining!H:h} works' '
	cat <<-EOF >expiration.in &&
{"id": 1447588528128, "state": 8,  "expiration": 0 }
	EOF
	expiration=$(cat expiration.in | \
	flux jobs --from-stdin -no "{expiration!D:h}") &&
	t_remaining=$(cat expiration.in | \
		     flux jobs --from-stdin -no "{t_remaining!H:h}") &&
	test_debug "echo expiration=$expiration, t_remaining=$t_remaining" &&
	test "${expiration}" = "-" &&
	test "${t_remaining}" = "-"
'
# note that a significant amount of annotation format tests occur in
# job-manager tests such as t2206-job-manager-annotate.t

test_expect_success 'flux-jobs annotation "sched" short hands work' '
	fmt="{annotations.sched},{annotations.sched.resource_summary}" &&
	flux jobs -no "${fmt}" > sched_long_hand.out &&
	fmt="{sched},{sched.resource_summary}" &&
	flux jobs -no "${fmt}" > sched_short_hand.out &&
	test_cmp sched_long_hand.out sched_short_hand.out
'

test_expect_success 'flux-jobs emits empty string on invalid annotations fields' '
	fmt="{annotations.foo},{annotations.foo:h}" &&
	fmt="${fmt},{annotations.sched.bar},{annotations.sched.bar:h}" &&
	fmt="${fmt},{annotations.x.y.z},{annotations.x.y.z:h}" &&
	flux jobs -no "${fmt}" >invalid-annotations.out 2>&1 &&
	test_debug "cat invalid-annotations.out" &&
	for i in `seq 1 $(job_list_state_count active)`; do
		echo ",-,,-,,-" >> invalid-annotations.exp
	done &&
	test_cmp invalid-annotations.out invalid-annotations.exp
'

test_expect_success 'flux-jobs "user" short hands work for job memo' '
	for id in $(job_list_state_ids sched); do
		flux job memo $id foo=42
	done &&
	fmt="{annotations.user},{annotations.user.foo}" &&
	flux jobs -no "${fmt}" > user_long_hand.out &&
	fmt="{user},{user.foo}" &&
	flux jobs -no "${fmt}" > user_short_hand.out &&
	test_cmp user_long_hand.out user_short_hand.out
'

test_expect_success 'flux-jobs emits empty string for special case t_estimate' '
	fmt="{annotations.sched.t_estimate}" &&
	fmt="${fmt},{annotations.sched.t_estimate!d:%H:%M}" &&
	fmt="${fmt},{annotations.sched.t_estimate!D}" &&
	fmt="${fmt},{annotations.sched.t_estimate!F}" &&
	fmt="${fmt},{annotations.sched.t_estimate!H}" &&
	fmt="${fmt},{annotations.sched.t_estimate!d:%H:%M::h}" &&
	fmt="${fmt},{annotations.sched.t_estimate!D:h}" &&
	fmt="${fmt},{annotations.sched.t_estimate!F:h}" &&
	fmt="${fmt},{annotations.sched.t_estimate!H:h}" &&
	flux jobs -no "${fmt}" >t_estimate_annotations.out 2>&1 &&
	test_debug "cat t_estimate_annotations.out" &&
	for i in `seq 1 $(job_list_state_count active)`; do
		echo ",,,,,-,-,-,-" >> t_estimate_annotations.exp
	done &&
	test_cmp t_estimate_annotations.out t_estimate_annotations.exp
'

test_expect_success 'FLUX_JOBS_FORMAT_DEFAULT works' '
	FLUX_JOBS_FORMAT_DEFAULT="{id} {id}" flux jobs > env_format.out &&
	grep "JOBID JOBID" env_format.out
'

test_expect_success 'FLUX_JOBS_FORMAT_DEFAULT works with named format' '
	FLUX_JOBS_FORMAT_DEFAULT=long flux jobs > env_format2.out &&
	grep "STATUS" env_format2.out
'

#
# project/name require update to jobs, do these "last" amongst the
# basic tests to avoid affecting tests as we modify fields.
#

test_expect_success 'flux-jobs --format={project},{bank} works (initially empty)' '
	flux jobs --filter=pending -no "{project},{bank}" > jobprojectbankP.out &&
	for i in `seq 1 $(job_list_state_count sched)`; do
		echo "," >> jobprojectbankP.exp
	done &&
	test_cmp jobprojectbankP.out jobprojectbankP.exp
'

test_expect_success 'support jobspec updates of project and bank' '
	flux jobtap load --remove=all ${PLUGINPATH}/project-bank-validate.so
'

test_expect_success 'update project and bank in pending jobs' '
	cat sched.ids | while read jobid; do
		flux update $jobid project=foo
		flux update $jobid bank=bar
	done
'

wait_project_bank_synced() {
	local i=0
	while [ "$(flux jobs -f pending -o {project} | grep foo | wc -l)" != "$(job_list_state_count sched)" ] \
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

# can be racy, need to wait until `job-list` module definitely has
# read from updated journal
test_expect_success 'flux-jobs --format={project},{bank} works (set)' '
	wait_project_bank_synced &&
	flux jobs --filter=pending -no "{project},{bank}" > jobprojectbankP2.out &&
	for i in `seq 1 $(job_list_state_count sched)`; do
		echo "foo,bar" >> jobprojectbankP2.exp
	done &&
	test_cmp jobprojectbankP2.out jobprojectbankP2.exp
'

test_expect_success 'remove jobtap plugins' '
	flux jobtap remove all
'


#
# format header tests.
#
# to add additional tests, simply add a line with custom format and
# expected header, separated by '=='
#
test_expect_success 'flux-jobs: header included with all custom formats' '
	cat <<-EOF >headers.expected &&
	id==JOBID
	id.f58==JOBID
	id.f58plain==JOBID
	id.hex==JOBID
	id.dothex==JOBID
	id.words==JOBID
	userid==UID
	username==USER
	urgency==URG
	priority==PRI
	state==STATE
	state_single==S
	state_emoji==STATE
	name==NAME
	cwd==CWD
	queue==QUEUE
	project==PROJECT
	bank==BANK
	ntasks==NTASKS
	duration==DURATION
	nnodes==NNODES
	ranks==RANKS
	nodelist==NODELIST
	contextual_info==INFO
	contextual_time==TIME
	inactive_reason==INACTIVE-REASON
	success==SUCCESS
	exception.occurred==EXCEPTION-OCCURRED
	exception.severity==EXCEPTION-SEVERITY
	exception.type==EXCEPTION-TYPE
	exception.note==EXCEPTION-NOTE
	result==RESULT
	result_abbrev==RS
	result_emoji==RESULT
	t_submit==T_SUBMIT
	t_depend==T_DEPEND
	t_run==T_RUN
	t_cleanup==T_CLEANUP
	t_inactive==T_INACTIVE
	runtime==RUNTIME
	runtime!F==RUNTIME
	runtime!H==RUNTIME
	expiration==EXPIRATION
	expiration!D==EXPIRATION
	expiration!d:%FT%T==EXPIRATION
	t_remaining==T_REMAINING
	t_remaining!F==T_REMAINING
	t_remaining!H==T_REMAINING
	t_inactive!d==T_INACTIVE
	t_cleanup!D==T_CLEANUP
	t_run!F==T_RUN
	status==STATUS
	status_abbrev==ST
	status_emoji==STATUS
	annotations==ANNOTATIONS
	annotations.sched==SCHED
	annotations.sched.t_estimate==T_ESTIMATE
	annotations.sched.reason_pending==REASON
	annotations.sched.resource_summary==RESOURCES
	annotations.sched.foobar==SCHED.FOOBAR
	sched==SCHED
	sched.t_estimate==T_ESTIMATE
	sched.reason_pending==REASON
	sched.resource_summary==RESOURCES
	sched.foobar==SCHED.FOOBAR
	user==USER
	user.foobar==USER.FOOBAR
	waitstatus==WSTATUS
	returncode==RC
	EOF
	sed "s/\(.*\)==.*/\1=={\1}/" headers.expected > headers.fmt &&
	flux jobs --from-stdin --format="$(cat headers.fmt)" \
	    > headers.output </dev/null &&
	test_cmp headers.expected headers.output
'

#
# color tests
#

check_no_color() {
	local file=$1
	count=`grep "\\u001b" $file | wc -l`
	if [ "$count" -eq 0 ]; then
		return 0
	fi
	return 1
}

no_color_lines() {
	grep -v -c "\\u001b" $1 || true
}
green_line_count() {
	grep -c -o "\\u001b\[01\;32m" $1 || true
}
red_line_count() {
	grep -c -o "\\u001b\[01\;31m" $1 || true
}
grey_line_count() {
	grep -c -o "\\u001b\[37m" $1 || true
}

for opt in "" "--color" "--color=always" "--color=auto"; do
	test_expect_success "flux-jobs $opt color works (pty)" '
		name=${opt##--color=} &&
		outfile=color-${name:-default}.out &&
		$runpty flux jobs ${opt} --no-header -a \
		    | grep -v "version" > $outfile &&
		count=$(no_color_lines $outfile) &&
		test $count -eq $(job_list_state_count sched run) &&
		count=$(green_line_count $outfile) &&
		test $count -eq $(job_list_state_count completed) &&
		count=$(red_line_count $outfile) &&
		test $count -eq $(job_list_state_count failed timeout) &&
		count=$(grey_line_count $outfile) &&
		test $count -eq $(job_list_state_count canceled)
	'
done

test_expect_success 'flux-jobs --color=never works (pty)' '
	$runpty flux jobs --no-header --color=never -a >color_never.out &&
	check_no_color color_never.out
'

for opt in "" "--color=never"; do
	test_expect_success "flux-jobs $opt color works (no tty)" '
		name=${opt##--color=} &&
		outfile=color-${name:-default}-notty.out &&
		flux jobs ${opt} --no-header -a > $outfile &&
		test_must_fail grep "" $outfile
	'
done

test_expect_success 'flux-jobs: --color=always works (notty)' '
	flux jobs --color=always --no-header -a > color-always-notty.out &&
	grep "" color-always-notty.out
'

#
# json tests
#

test_expect_success 'flux-jobs: --json does not work with --stats' '
	test_must_fail flux jobs --stats --json &&
	test_must_fail flux jobs --stats-only --json
'

#  Multiple jobs return an array
#  Active jobs do not have result
test_expect_success 'flux-jobs: --json option works' '
	flux jobs --json >jobs.json &&
	jq -e ".jobs[0].id" < jobs.json &&
	jq -e ".jobs[0] | .result | not" < jobs.json &&
	jq -e ".jobs[0] | .t_cleanup | not" < jobs.json &&
	jq -e ".jobs[0] | .exception.occurred == false" < jobs.json
'

#  Ensure single jobid argument returns single JSON object
test_expect_success 'flux-jobs: --json option works with one jobid' '
	testid=$(cat active.ids | head -1 | flux job id) &&
	flux jobs --json $testid | jq -e ".id == $testid"
'

#  Ensure failed job has some expected fields
test_expect_success 'flux-jobs: --json works for inactive job' '
	testid=$(cat failed.ids | head -1 | flux job id) &&
	flux jobs --json $testid > failedjob.json &&
	test_debug "jq < failedjob.json" &&
	jq -e ".id == $testid" < failedjob.json &&
	jq -e ".result == \"FAILED\"" < failedjob.json &&
	jq -e ".success == false" < failedjob.json &&
	jq -e ".state == \"INACTIVE\"" < failedjob.json &&
	jq -e ".t_cleanup > 0" < failedjob.json &&
	jq -e ".t_run > 0" < failedjob.json &&
	jq -e ".runtime > 0" < failedjob.json &&
	jq -e ".exception.occurred == true" < failedjob.json &&
	jq -e ".exception.type" < failedjob.json
'

#  Asking for a specific nonexisting jobid returns no output
test_expect_success 'flux-jobs: --json with missing jobid returns nothing' '
	flux jobs --json 123 >missing.json &&
	test_must_be_empty missing.json
'

#
# corner cases
#

test_expect_success 'flux-jobs illegal count leads to RPC error' '
	test_must_fail flux jobs --count=-1
'

test_expect_success 'flux-jobs --format with illegal field is an error' '
	test_must_fail flux jobs --format="{foobar}"
'

test_expect_success 'flux-jobs illegal color options is an error' '
	test_must_fail flux jobs --color=foobar
'

test_expect_success 'flux-jobs --from-stdin works with no input' '
	flux jobs --from-stdin </dev/null
'

test_expect_success 'flux-jobs --from-stdin fails with invalid input' '
	echo foo | test_must_fail flux jobs --from-stdin
'

find_invalid_userid() {
	flux python -c 'import pwd; \
	                ids = [e.pw_uid for e in pwd.getpwall()]; \
	                print (next(i for i in range(65536) if not i in ids));'
}

test_expect_success 'flux-jobs reverts username to userid for invalid ids' '
	id=$(find_invalid_userid) &&
	test_debug "echo first invalid userid is ${id}" &&
	printf "%s\n" $id > invalid_userid.expected &&
	flux job list -a -c 1 | $jq -c ".userid = ${id}" |
	  flux jobs --from-stdin --no-header --format="{username}" \
		> invalid_userid.output  &&
	test_cmp invalid_userid.expected invalid_userid.output
'

#
# regression tests
#
#  Iterate over flux-job tests dirs in t/flux-jobs/tests/*
#  Each directory should have the following files:
#
#   - input:        input to be read by flux jobs --from-stdin
#   - output:       expected output
#   - format:       (optional) alternate --format arg to flux-jobs
#   - description:  (optional) informational description of this test
#
ISSUES_DIR=$SHARNESS_TEST_SRCDIR/flux-jobs/tests
for d in ${ISSUES_DIR}/*; do
	issue=$(basename $d)
	for f in ${d}/input ${d}/output; do
		test -f ${f}  || error "Missing required file ${f}"
	done
	desc=$(basename ${d})
	if test -f ${d}/description; then
		desc="${desc}: $(cat ${d}/description)"
	fi
	if test -f ${d}/format; then
		fmt=$(cat ${d}/format)
	else
		fmt=""
	fi
	test_expect_success "${desc}" '
		flux jobs -n --from-stdin ${fmt:+--format="$fmt"} < ${d}/input >${issue}.output &&
		test_cmp ${d}/output ${issue}.output
	'
done


# job stats
test_expect_success 'flux-jobs --stats works (global)' '
	flux jobs --stats -a >stats.output &&
	test_debug "cat stats.output" &&
	fail=$(job_list_state_count failed canceled timeout) &&
	run=$(job_list_state_count run) &&
	active=$(job_list_state_count active) &&
	comp=$(job_list_state_count completed) &&
	pend=$((active - run)) &&
	cat <<-EOF >stats.expected &&
	${run} running, ${comp} completed, ${fail} failed, ${pend} pending, 0 inactive purged
	EOF
	head -1 stats.output > stats.actual &&
	test_cmp stats.expected stats.actual
'

test_expect_success 'flux-jobs --stats works (queue1)' '
	flux jobs --stats -a --queue=queue1 >statsq1.output &&
	test_debug "cat statsq1.output" &&
	comp=$(job_list_state_count completed) &&
	cat <<-EOF >statsq1.expected &&
	0 running, ${comp} completed, 0 failed, 0 pending, 0 inactive purged
	EOF
	head -1 statsq1.output > statsq1.actual &&
	test_cmp statsq1.expected statsq1.actual
'

test_expect_success 'flux-jobs --stats works (queue2)' '
	flux jobs --stats -a --queue=queue2 >statsq2.output &&
	test_debug "cat statsq2.output" &&
	run=$(job_list_state_count run) &&
	active=$(job_list_state_count active) &&
	pend=$((active - run)) &&
	cat <<-EOF >statsq2.expected &&
	${run} running, 0 completed, 0 failed, ${pend} pending, 0 inactive purged
	EOF
	head -1 statsq2.output > statsq2.actual &&
	test_cmp statsq2.expected statsq2.actual
'

test_expect_success 'flux-jobs --stats works (defaultqueue)' '
	flux jobs --stats -a --queue=defaultqueue >statsqdefault.output &&
	test_debug "cat statsqdefault.output" &&
	fail=$(job_list_state_count failed canceled timeout) &&
	cat <<-EOF >statsqdefault.expected &&
	0 running, 0 completed, ${fail} failed, 0 pending, 0 inactive purged
	EOF
	head -1 statsqdefault.output > statsqdefault.actual &&
	test_cmp statsqdefault.expected statsqdefault.actual
'

test_expect_success 'flux-jobs --stats-only works' '
	flux jobs --stats-only > stats-only.output &&
	test_cmp stats.expected stats-only.output
'

test_expect_success 'cleanup job listing jobs ' '
	flux cancel $(cat active.ids) &&
	for jobid in `cat active.ids`; do
		fj_wait_event $jobid clean;
	done
'

test_expect_success 'purge all jobs' '
	flux job purge --force --num-limit=0
'

#
# All stat tests below assume all jobs purged
#

test_expect_success 'flux-jobs --stats works after jobs purged (all)' '
	flux jobs --stats -a >statspurge.output &&
	test_debug "cat statspurge.output" &&
	all=$(job_list_state_count all) &&
	cat <<-EOF >statspurge.expected &&
	0 running, 0 completed, 0 failed, 0 pending, ${all} inactive purged
	EOF
	head -1 statspurge.output > statspurge.actual &&
	test_cmp statspurge.expected statspurge.actual
'

test_expect_success 'flux-jobs --stats works after jobs purged (queue1)' '
	flux jobs --stats -a --queue=queue1 >statspurgeq1.output &&
	test_debug "cat statspurgeq1.output" &&
	comp=$(job_list_state_count completed) &&
	cat <<-EOF >statspurgeq1.expected &&
	0 running, 0 completed, 0 failed, 0 pending, ${comp} inactive purged
	EOF
	head -1 statspurgeq1.output > statspurgeq1.actual &&
	test_cmp statspurgeq1.expected statspurgeq1.actual
'

test_expect_success 'flux-jobs --stats works after jobs purged (queue2)' '
	flux jobs --stats -a --queue=queue2 >statspurgeq2.output &&
	test_debug "cat statspurgeq2.output" &&
	active=$(job_list_state_count active) &&
	cat <<-EOF >statspurgeq2.expected &&
	0 running, 0 completed, 0 failed, 0 pending, ${active} inactive purged
	EOF
	head -1 statspurgeq2.output > statspurgeq2.actual &&
	test_cmp statspurgeq2.expected statspurgeq2.actual
'

test_expect_success 'flux-jobs --stats works after jobs purged (defaultqueue)' '
	flux jobs --stats -a --queue=defaultqueue >statspurgeqdefault.output &&
	test_debug "cat statspurgeqdefault.output" &&
	fail=$(job_list_state_count failed canceled timeout) &&
	cat <<-EOF >statspurgeqdefault.expected &&
	0 running, 0 completed, 0 failed, 0 pending, ${fail} inactive purged
	EOF
	head -1 statspurgeqdefault.output > statspurgeqdefault.actual &&
	test_cmp statspurgeqdefault.expected statspurgeqdefault.actual
'
test_expect_success 'flux-jobs allows sorting order in format' '
	flux jobs -ano "{ncores}	{t_submit}	{id.f58}" \
		| sort -k1,2n \
		| cut -f 3 >sort1.expected &&
	flux jobs -ano "sort:ncores,t_submit {id.f58}" >sort1.out &&
	test_cmp sort1.expected sort1.out
'
test_expect_success 'flux-jobs allows sorting order via --sort' '
	flux jobs -ano "{ntasks}	{t_submit}	{id.f58}" \
		| sort -k1n,2rn \
		| cut -f 3 >sort2.expected &&
	flux jobs -an --sort=ntasks,-t_submit -o "{id.f58}" >sort2.out &&
	test_cmp sort2.expected sort2.out
'
test_expect_success 'flux-jobs --sort overrides sort: in format' '
	flux jobs -ano "{ntasks}	{t_submit}	{id.f58}" \
		| sort -k1n,2rn \
		| cut -f 3 >sort3.expected &&
	flux jobs -an --sort=ntasks,-t_submit -o "sort:t_submit {id.f58}" \
		>sort3.out &&
	test_cmp sort3.expected sort3.out
'
test_expect_success 'flux-jobs invalid sort key throws exception' '
	test_must_fail flux jobs -a --sort=foo 2>sort.error &&
	grep "Invalid sort key: foo" sort.error
'
test_done
