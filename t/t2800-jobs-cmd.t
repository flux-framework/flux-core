#!/bin/sh

test_description='Test flux jobs command'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py --line-buffer -f asciicast"

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
# the job-list module has eventual consistency with the jobs stored in
# the job-manager's queue.  To ensure no raciness in tests, we spin
# until all of the pending jobs have reached SCHED state, running jobs
# have reached RUN state, and inactive jobs have reached INACTIVE
# state.
#

# Return the expected jobids list in a given state:
#   "all", "run", "sched", "active", "inactive",
#   "completed", "canceled", "failed", "timeout'
#
state_ids() {
	for f in "$@"; do
		cat ${f}.ids
	done
}

# Return the expected count of jobs in a given state (See above for list)
#
state_count() {
	state_ids "$@" | wc -l
}

wait_states() {
	sched=$(state_count sched)
	run=$(state_count run)
	inactive=$(state_count inactive)
	local i=0
	printf >&2 "Waiting for sched=$sched run=$run inactive=$inactive\n"
	while ( [ "$(flux jobs -n --filter=sched | wc -l)" != "$sched" ] \
	     || [ "$(flux jobs -n --filter=run | wc -l)" != "$run" ] \
	     || [ "$(flux jobs -n --filter=inactive | wc -l)" != "$inactive" ]) \
	&& [ $i -lt 50 ]
	do
		sleep 0.1
		i=$((i + 1))
	done
	if [ "$i" -eq "50" ]; then
		return 1
	fi
	return 0
}

export FLUX_PYCLI_LOGLEVEL=10

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

listjobs() {
	${FLUX_BUILD_DIR}/t/job-manager/list-jobs \
	    | $jq .id \
	    | flux job id --to=f58
}

test_expect_success HAVE_JQ 'submit jobs for job list testing' '
	#  Create `hostname` and `sleep` jobspec
	#  N.B. Used w/ `flux job submit` for serial job submission
	#  for efficiency (vs serial `flux mini submit`.
	#
	flux mini submit --dry-run hostname >hostname.json &&
	flux mini submit --dry-run --time-limit=5m sleep 600 > sleeplong.json &&
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
	! jobid=`flux mini submit --wait nosuchcommand` &&
	echo $jobid >> inactiveids &&
	echo $jobid > failed.ids &&
	#
	#  Run a job that will timeout, copy its JOBID to both inactive and
	#   timeout lists.
	#
	jobid=`flux mini submit --time-limit=0.5s sleep 30` &&
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
	for u in 31 25 20 15 10 5; do
		flux job submit --urgency=$u sleeplong.json >> sched.ids
	done &&
	listjobs > active.ids &&
	#
	#  Submit a job and cancel it
	#
	jobid=`flux mini submit --job-name=canceledjob sleep 30` &&
	fj_wait_event $jobid depend &&
	flux job cancel $jobid &&
	fj_wait_event $jobid clean &&
	echo $jobid >> inactiveids &&
	echo $jobid > canceled.ids &&
	tac inactiveids > inactive.ids &&
	cat inactive.ids active.ids >> all.ids &&
	#
	#  Synchronize all expected states
	wait_states
'

#
# basic tests
#
test_expect_success 'flux-jobs --suppress-header works' '
	count=`flux jobs --suppress-header | wc -l` &&
	test $count -eq $(state_count active)
'
test_expect_success 'flux-jobs default output works' '
	flux jobs -n > default.out &&
	test $(wc -l < default.out) -eq $(state_count active) &&
	test $(grep -c "  S " default.out) -eq $(state_count sched) &&
	test $(grep -c "  R " default.out) -eq $(state_count run) &&
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

# TODO: need to submit jobs as another user and test -A again
test_expect_success 'flux-jobs -a and -A works' '
	nall=$(state_count all) &&
	count=`flux jobs --suppress-header -a | wc -l` &&
	test $count -eq $nall &&
	count=`flux jobs --suppress-header -a -A | wc -l` &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --since implies -a' '
	nall=$(state_count all) &&
	count=$(flux jobs --suppress-header --since=0.0 | wc -l) &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --since with --filter does not imply -a' '
	nfailed=$(state_count failed) &&
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
	nall=$(state_count all) &&
	count=$(flux jobs -n --since="a year ago" | wc -l) &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --since works with fsd offset' '
	nall=$(state_count all) &&
	count=$(flux jobs -n --since=-8.8h | wc -l) &&
	test $count -eq $nall
'

test_expect_success 'flux-jobs --name works' '
	test_debug "flux jobs -an --name=nosuchcommand" &&
	test $(flux jobs -an --name=nosuchcommand | wc -l) -eq 1 &&
	test_debug "flux jobs -an --name=xxyyzz" &&
	test $(flux jobs -an --name=xxyyzz | wc -l) -eq 0
'

# Recall pending = depend | priority | sched, running = run | cleanup,
#  active = pending | running
test_expect_success 'flux-jobs --filter works (job states)' '
	count=`flux jobs --suppress-header --filter=depend | wc -l` &&
	test $count -eq 0 &&
	count=`flux jobs --suppress-header --filter=priority | wc -l` &&
	test $count -eq 0 &&
	count=`flux jobs --suppress-header --filter=sched | wc -l` &&
	test $count -eq $(state_count sched) &&
	count=`flux jobs --suppress-header --filter=pending | wc -l` &&
	test $count -eq $(state_count sched) &&
	count=`flux jobs --suppress-header --filter=run | wc -l` &&
	test $count -eq $(state_count run) &&
	count=`flux jobs --suppress-header --filter=cleanup | wc -l` &&
	test $count -eq 0 &&
	count=`flux jobs --suppress-header --filter=running | wc -l` &&
	test $count -eq $(state_count run) &&
	count=`flux jobs --suppress-header --filter=inactive | wc -l` &&
	test $count -eq $(state_count inactive) &&
	count=`flux jobs --suppress-header --filter=pending,running | wc -l` &&
	test $count -eq $(state_count sched run) &&
	count=`flux jobs --suppress-header --filter=sched,run | wc -l` &&
	test $count -eq $(state_count sched run) &&
	count=`flux jobs --suppress-header --filter=active | wc -l` &&
	test $count -eq $(state_count active) &&
	count=`flux jobs --suppress-header --filter=depend,priority,sched,run,cleanup | wc -l` &&
	test $count -eq $(state_count active) &&
	count=`flux jobs --suppress-header --filter=pending,inactive | wc -l` &&
	test $count -eq $(state_count sched inactive) &&
	count=`flux jobs --suppress-header --filter=sched,inactive | wc -l` &&
	test $count -eq $(state_count sched inactive) &&
	count=`flux jobs --suppress-header --filter=running,inactive | wc -l` &&
	test $count -eq $(state_count run inactive) &&
	count=`flux jobs --suppress-header --filter=run,inactive | wc -l` &&
	test $count -eq $(state_count run inactive) &&
	count=`flux jobs --suppress-header --filter=pending,running,inactive | wc -l` &&
	test $count -eq $(state_count all) &&
	count=`flux jobs --suppress-header --filter=active,inactive | wc -l` &&
	test $count -eq $(state_count active inactive) &&
	count=`flux jobs --suppress-header --filter=depend,priority,cleanup | wc -l` &&
	test $count -eq 0
'

test_expect_success 'flux-jobs --filter works (job results)' '
	count=`flux jobs --suppress-header --filter=completed | wc -l` &&
	test $count -eq $(state_count completed) &&
	count=`flux jobs --suppress-header --filter=failed | wc -l` &&
	test $count -eq $(state_count failed) &&
	count=`flux jobs --suppress-header --filter=canceled | wc -l` &&
	test $count -eq $(state_count canceled) &&
	count=`flux jobs --suppress-header --filter=timeout | wc -l` &&
	test $count -eq $(state_count timeout) &&
	count=`flux jobs --suppress-header --filter=completed,failed | wc -l` &&
	test $count -eq $(state_count completed failed) &&
	count=`flux jobs --suppress-header --filter=completed,canceled | wc -l` &&
	test $count -eq $(state_count completed canceled) &&
	count=`flux jobs --suppress-header --filter=completed,timeout | wc -l` &&
	test $count -eq $(state_count completed timeout) &&
	count=`flux jobs --suppress-header --filter=completed,failed,canceled | wc -l` &&
	test $count -eq $(state_count completed failed canceled) &&
	count=`flux jobs --suppress-header --filter=completed,failed,timeout | wc -l` &&
	test $count -eq $(state_count completed failed timeout) &&
	count=`flux jobs --suppress-header --filter=completed,failed,canceled,timeout | wc -l` &&
	test $count -eq $(state_count completed failed canceled timeout) &&
	count=`flux jobs --suppress-header --filter=pending,completed | wc -l` &&
	test $count -eq $(state_count sched completed) &&
	count=`flux jobs --suppress-header --filter=pending,failed | wc -l` &&
	test $count -eq $(state_count sched failed) &&
	count=`flux jobs --suppress-header --filter=pending,canceled | wc -l` &&
	test $count -eq $(state_count sched canceled) &&
	count=`flux jobs --suppress-header --filter=pending,timeout | wc -l` &&
	test $count -eq $(state_count sched timeout) &&
	count=`flux jobs --suppress-header --filter=running,completed | wc -l` &&
	test $count -eq $(state_count run completed) &&
	count=`flux jobs --suppress-header --filter=running,failed | wc -l` &&
	test $count -eq $(state_count run failed) &&
	count=`flux jobs --suppress-header --filter=running,canceled | wc -l` &&
	test $count -eq $(state_count run canceled) &&
	count=`flux jobs --suppress-header --filter=running,timeout | wc -l` &&
	test $count -eq $(state_count run timeout)
'


test_expect_success 'flux-jobs --filter with invalid state fails' '
	test_must_fail flux jobs --filter=foobar 2> invalidstate.err &&
	grep "Invalid filter specified: foobar" invalidstate.err
'

# ensure + prefix works
# increment userid to ensure not current user for test
test_expect_success 'flux-jobs --user=UID works' '
	userid=`id -u` &&
	count=`flux jobs --suppress-header --user=${userid} | wc -l` &&
	test $count -eq $(state_count active) &&
	count=`flux jobs --suppress-header --user="+${userid}" | wc -l` &&
	test $count -eq $(state_count active) &&
	userid=$((userid+1)) &&
	count=`flux jobs --suppress-header --user=${userid} | wc -l` &&
	test $count -eq 0
'

test_expect_success 'flux-jobs --user=USERNAME works' '
	username=`whoami` &&
	count=`flux jobs --suppress-header --user=${username} | wc -l` &&
	test $count -eq $(state_count active)
'

test_expect_success 'flux-jobs --user with invalid username fails' '
	username="foobarfoobaz" &&
	test_must_fail flux jobs --suppress-header --user=${username} 2> baduser.out &&
	grep "Invalid user" baduser.out
'

test_expect_success 'flux-jobs --user=all works' '
	count=`flux jobs --suppress-header --user=all | wc -l` &&
	test $count -eq $(state_count active)
'

test_expect_success 'flux-jobs --count works' '
	count=`flux jobs --suppress-header -a --count=0 | wc -l` &&
	test $count -eq $(state_count all) &&
	count=`flux jobs --suppress-header -a --count=8 | wc -l` &&
	test $count -eq 8
'

#
# test specific IDs
#

test_expect_success 'flux-jobs specific IDs works' '
	for state in sched run inactive; do
		ids=$(state_ids $state) &&
		expected=$(state_count $state) &&
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
	flux jobs --suppress-header 0 1 2 2> ids.err &&
	count=`grep -i unknown ids.err | wc -l` &&
	test $count -eq 3
'

test_expect_success 'flux-jobs errors with illegal IDs' '
	test_must_fail flux jobs --suppress-header IllegalID 2> illegal_ids.err &&
	grep "invalid JobID value" illegal_ids.err
'

test_expect_success 'flux-jobs good and bad IDs works' '
	ids=$(state_ids sched) &&
	flux jobs --suppress-header ${ids} 0 1 2 > ids.out 2> ids.err &&
	count=`wc -l < ids.out` &&
	test $count -eq $(state_count sched) &&
	count=`grep -i unknown ids.err | wc -l` &&
	test $count -eq 3
'

test_expect_success 'flux-jobs ouputs warning on invalid options' '
	ids=$(state_ids sched) &&
	flux jobs --suppress-header -A ${ids} > warn.out 2> warn.err &&
	grep WARNING warn.err
'

#
# format tests
#

test_expect_success 'flux-jobs --format={id} works' '
	flux jobs --suppress-header --filter=pending --format="{id}" > idsP.out &&
	test_cmp idsP.out sched.ids &&
	flux jobs --suppress-header --filter=running --format="{id}" > idsR.out &&
	test_cmp idsR.out run.ids &&
	flux jobs --suppress-header --filter=inactive --format="{id}" > idsI.out &&
	test_cmp idsI.out inactive.ids
'

test_expect_success 'flux-jobs --format={id.f58},{id.hex},{id.dothex},{id.words} works' '
	flux jobs -ano {id.dec},{id.f58},{id.hex},{id.kvs},{id.dothex},{id.words} \
	    | sort -n > ids.XX.out &&
	for id in $(cat all.ids); do
		printf "%s,%s,%s,%s,%s,%s\n" \
		       $(flux job id --to=dec $id) \
		       $(flux job id --to=f58 $id) \
		       $(flux job id --to=hex $id) \
		       $(flux job id --to=kvs $id) \
		       $(flux job id --to=dothex $id) \
		       $(flux job id --to=words $id)
	done | sort -n > ids.XX.expected &&
	test_cmp ids.XX.expected ids.XX.out
'

test_expect_success 'flux-jobs --format={userid},{username} works' '
	flux jobs --suppress-header -a --format="{userid},{username}" > user.out &&
	id=`id -u` &&
	name=`whoami` &&
	for i in `seq 1 $(state_count all)`; do
		echo "${id},${name}" >> user.exp
	done &&
	test_cmp user.out user.exp
'

test_expect_success 'flux-jobs --format={urgency},{priority} works' '
	flux jobs --suppress-header -a --format="{urgency},{priority}" > urgency_priority.out &&
	echo 31,4294967295 > urgency_priority.exp &&
	echo 25,25 >> urgency_priority.exp &&
	echo 20,20 >> urgency_priority.exp &&
	echo 15,15 >> urgency_priority.exp &&
	echo 10,10 >> urgency_priority.exp &&
	echo 5,5 >> urgency_priority.exp &&
	for i in `seq 1 $(state_count run)`; do
		echo "16,16" >> urgency_priority.exp
	done &&
	for i in `seq 1 $(state_count inactive)`; do
		echo "16,16" >> urgency_priority.exp
	done &&
	test_cmp urgency_priority.out urgency_priority.exp
'

test_expect_success 'flux-jobs --format={state},{state_single} works' '
	flux jobs --filter=pending -c1 -no "{state},{state_single}" > stateP.out &&
	test "$(cat stateP.out)" = "SCHED,S" &&
	flux jobs --filter=running -c1 -no "{state},{state_single}" > stateR.out &&
	test "$(cat stateR.out)" = "RUN,R" &&
	flux jobs --filter=inactive -c1 -no "{state},{state_single}" > stateI.out &&
	test "$(cat stateI.out)" = "INACTIVE,I"
'

test_expect_success 'flux-jobs --format={name} works' '
	flux jobs --filter=pending,running -no "{name}" > jobnamePR.out &&
	for i in `seq 1 $(state_count run sched)`; do
		echo "sleep" >> jobnamePR.exp
	done &&
	test_cmp jobnamePR.out jobnamePR.exp &&
	flux jobs --filter=inactive -no "{name}" > jobnameI.out &&
	echo "canceledjob" >> jobnameI.exp &&
	echo "sleep" >> jobnameI.exp &&
	echo "nosuchcommand" >> jobnameI.exp &&
	count=$(($(state_count inactive) - 3)) &&
	for i in `seq 1 $count`; do
		echo "hostname" >> jobnameI.exp
	done &&
	test_cmp jobnameI.out jobnameI.exp
'

test_expect_success 'flux-jobs --format={ntasks},{nnodes},{nnodes:h} works' '
	flux jobs --filter=pending -no "{ntasks},{nnodes},{nnodes:h}" > nodecountP.out &&
	for i in `seq 1 $(state_count sched)`; do
		echo "1,,-" >> nodecountP.exp
	done &&
	test_cmp nodecountP.exp nodecountP.out &&
	flux jobs --filter=running -no "{ntasks},{nnodes},{nnodes:h}" > nodecountR.out &&
	for i in `seq 1 $(state_count run)`; do
		echo "1,1,1" >> nodecountR.exp
	done &&
	test_cmp nodecountR.exp nodecountR.out &&
	flux jobs --filter=inactive -no "{ntasks},{nnodes},{nnodes:h}" > nodecountI.out &&
	echo "1,,-" > nodecountI.exp &&
	for i in `seq 1 $(state_count completed failed timeout)`;
		do echo "1,1,1" >> nodecountI.exp
	done &&
	test_cmp nodecountI.exp nodecountI.out
'


test_expect_success 'flux-jobs --format={runtime:0.3f} works' '
	flux jobs --filter=pending -no "{runtime:0.3f}" > runtime-dotP.out &&
	for i in `seq 1 $(state_count sched)`;
		do echo "0.000" >> runtime-dotP.exp;
	done &&
	test_cmp runtime-dotP.out runtime-dotP.exp &&
	flux jobs --filter=running,inactive -no "{runtime:0.3f}" > runtime-dotRI.out &&
	[ "$(grep -E "\.[0-9]{3}" runtime-dotRI.out | wc -l)" = "15" ]
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
	for i in `seq 1 $(state_count sched)`; do
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
	flux jobs -no "{ranks},{ranks:h}" $(state_ids completed) > ranksCD.out &&
	test_debug "cat ranksCD.out" &&
	test "$(sort -n ranksCD.out | head -1)" = "0,0" &&
	flux jobs -no "{ranks},{ranks:h}" $(state_ids canceled) > ranksCA.out &&
	test_debug "cat ranksCA.out" &&
	test "$(sort -n ranksCA.out | head -1)" = ",-" &&
	flux jobs -no "{ranks},{ranks:h}" $(state_ids timeout) > ranksTO.out &&
	test_debug "cat ranksTO.out" &&
	test "$(sort -n ranksTO.out | head -1)" = "0,0"
'

test_expect_success 'flux-jobs --format={nodelist},{nodelist:h} works' '
	flux jobs --filter=pending -no "{nodelist},{nodelist:h}" > nodelistP.out &&
	for i in `seq 1 $(state_count sched)`; do
		echo ",-" >> nodelistP.exp
	done &&
	test_cmp nodelistP.out nodelistP.exp &&
	flux jobs --filter=running -no "{nodelist},{nodelist:h}" > nodelistR.out &&
	for id in $(state_ids run); do
		nodes=`flux job info ${id} R | flux R decode --nodelist`
		echo "${nodes},${nodes}" >> nodelistR.exp
	done &&
	test_debug "cat nodelistR.out" &&
	test_cmp nodelistR.out nodelistR.exp &&
	flux jobs -no "{nodelist},{nodelist:h}" $(state_ids completed timeout) > nodelistCDTO.out &&
	for id in $(state_ids completed timeout); do
		nodes=`flux job info ${id} R | flux R decode --nodelist`
		echo "${nodes},${nodes}" >> nodelistCDTO.exp
	done &&
	test_debug "cat nodelistCDTO.out" &&
	test_cmp nodelistCDTO.out nodelistCDTO.exp &&
	flux jobs -no "{nodelist},{nodelist:h}" $(state_ids canceled) > nodelistCA.out &&
	test_debug "cat nodelistCA.out" &&
	echo ",-" > nodelistCA.exp &&
	test_cmp nodelistCA.out nodelistCA.exp
'

# test just make sure numbers are zero or non-zero given state of job
test_expect_success 'flux-jobs --format={t_submit/depend/sched} works' '
	flux jobs -ano "{t_submit},{t_depend}" >t_SD.out &&
	count=`cut -d, -f1 t_SD.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(state_count all) &&
	count=`cut -d, -f2 t_SD.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(state_count all)
'
test_expect_success 'flux-jobs --format={t_run} works' '
	flux jobs --filter=pending -no "{t_run},{t_run:h}" > t_runP.out &&
	flux jobs --filter=running -no "{t_run}" > t_runR.out &&
	flux jobs --filter=inactive -no "{t_run}" > t_runI.out &&
	count=`cut -d, -f1 t_runP.out | grep "^0.0$" | wc -l` &&
	test $count -eq $(state_count sched) &&
	count=`cut -d, -f2 t_runP.out | grep "^-$" | wc -l` &&
	test $count -eq $(state_count sched) &&
	count=`cat t_runR.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(state_count run) &&
	cat t_runI.out &&
	count=`head -n 1 t_runI.out | grep "^0.0$" | wc -l` &&
	test $count -eq 1 &&
	count=`tail -n $(state_count completed timeout) t_runI.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(state_count completed timeout)
'
test_expect_success 'flux jobs --format={t_cleanup/{in}active} works' '
	flux jobs --filter=pending,running -no "{t_cleanup},{t_cleanup:h},{t_inactive},{t_inactive:h}" > t_cleanupPR.out &&
	flux jobs --filter=inactive -no "{t_cleanup},{t_inactive}" > t_cleanupI.out &&
	count=`cut -d, -f1 t_cleanupPR.out | grep "^0.0$" | wc -l` &&
	test $count -eq $(state_count sched run) &&
	count=`cut -d, -f2 t_cleanupPR.out | grep "^-$" | wc -l` &&
	test $count -eq $(state_count sched run) &&
	count=`cut -d, -f1 t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(state_count inactive) &&
	count=`cut -d, -f3 t_cleanupPR.out | grep "^0.0$" | wc -l` &&
	test $count -eq $(state_count sched run) &&
	count=`cut -d, -f4 t_cleanupPR.out | grep "^-$" | wc -l` &&
	test $count -eq $(state_count sched run) &&
	count=`cut -d, -f2 t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
	test $count -eq $(state_count inactive)
'

test_expect_success 'flux-jobs --format={runtime},{runtime!F},{runtime!F:h},{runtime!H},{runtime!H:h} works' '
	fmt="{runtime},{runtime!F},{runtime!H},{runtime!F:h},{runtime!H:h}" &&
	flux jobs --filter=pending -no "${fmt}" > runtimeP.out &&
	for i in `seq 1 $(state_count sched)`; do
		echo "0.0,0s,0:00:00,-,-" >> runtimeP.exp
	done &&
	test_cmp runtimeP.out runtimeP.exp &&
	runcount=$(state_count run) &&
	flux jobs --filter=running -no "${fmt}" > runtimeR.out &&
	i=1 &&
	for nomatch in 0.0 0s 0:00:00 - -; do
		name=$(echo $fmt | cut -d, -f${i}) &&
		count=$(cut -d, -f${i} runtimeR.out | grep -v "^${nomatch}$" | wc -l) &&
		test_debug "echo $name field $i ${nomatch} $count/$runcount times" &&
		test $count -eq $runcount &&
		i=$((i+1))
	done &&
	expected=$(state_count completed timeout) &&
	flux jobs -no "$fmt" $(state_ids completed timeout) >runtimeCDTO.out &&
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
	for i in `seq 1 $(state_count sched run)`; do
		echo ",-" >> successPR.exp;
	done &&
	test_cmp successPR.out successPR.exp &&
	flux jobs --filter=inactive -no "{success},{success:h}" > successI.out &&
	test $(grep -c False,False successI.out) -eq $(state_count failed canceled timeout) &&
	test $(grep -c True,True successI.out) -eq $(state_count completed)
'

test_expect_success 'flux-jobs --format={exception.*},{exception.*:h} works' '
	fmt="{exception.occurred},{exception.occurred:h}" &&
	fmt="${fmt},{exception.severity},{exception.severity:h}" &&
	fmt="${fmt},{exception.type},{exception.type:h}" &&
	fmt="${fmt},{exception.note},{exception.note:h}" &&
	flux jobs --filter=pending,running -no "$fmt" > exceptionPR.out &&
	count=$(grep -c "^,-,,-,,-,,-$" exceptionPR.out) &&
	test $count -eq $(state_count sched run) &&
	flux jobs --filter=inactive -no "$fmt" > exceptionI.out &&
	count=$(grep -c "^True,True,0,0,cancel,cancel,,-$" exceptionI.out) &&
	test $count -eq $(state_count canceled) &&
	count=$(grep -c "^True,True,0,0,exec,exec,.*No such file.*" exceptionI.out) &&
	test $count -eq $(state_count failed) &&
	count=$(grep -c "^True,True,0,0,timeout,timeout,.*expired.*" exceptionI.out) &&
	test $count -eq $(state_count timeout) &&
	count=$(grep -c "^False,False,,-,,-,," exceptionI.out) &&
	test $count -eq $(state_count completed)
'


test_expect_success 'flux-jobs --format={result},{result:h},{result_abbrev},{result_abbrev:h} works' '
	fmt="{result},{result:h},{result_abbrev},{result_abbrev:h}" &&
	flux jobs --filter=pending,running -no "$fmt" > resultPR.out &&
	count=$(grep -c "^,-,,-$" resultPR.out) &&
	test_debug "echo checking sched+run got $count" &&
	test $count -eq $(state_count sched run) &&
	flux jobs  --filter=inactive -no "$fmt" > resultI.out &&
	count=$(grep -c "CANCELED,CANCELED,CA,CA" resultI.out) &&
	test_debug "echo checking canceled got $count" &&
	test $count -eq $(state_count canceled) &&
	count=$(grep -c "FAILED,FAILED,F,F" resultI.out) &&
	test_debug "echo checking failed got $count" &&
	test $count -eq $(state_count failed) &&
	count=$(grep -c "TIMEOUT,TIMEOUT,TO,TO" resultI.out) &&
	test_debug "echo checking timeout got $count" &&
	test $count -eq $(state_count timeout) &&
	count=$(grep -c "COMPLETED,COMPLETED,CD,CD" resultI.out) &&
	test_debug "echo checking completed got $count" &&
	test $count -eq $(state_count completed)
'

test_expect_success 'flux-jobs --format={status},{status_abbrev} works' '
	flux jobs --filter=sched    -no "{status},{status_abbrev}" > statusS.out &&
	flux jobs --filter=run      -no "{status},{status_abbrev}" > statusR.out &&
	flux jobs --filter=inactive -no "{status},{status_abbrev}" > statusI.out &&
	count=$(grep -c "SCHED,S" statusS.out) &&
	test $count -eq $(state_count sched) &&
	count=$(grep -c "RUN,R" statusR.out) &&
	test $count -eq $(state_count run) &&
	count=$(grep -c "CANCELED,CA" statusI.out) &&
	test $count -eq $(state_count canceled) &&
	count=$(grep -c "FAILED,F" statusI.out) &&
	test $count -eq $(state_count failed) &&
	count=$(grep -c "TIMEOUT,TO" statusI.out) &&
	test $count -eq $(state_count failed) &&
	count=$(grep -c "COMPLETED,CD" statusI.out) &&
	test $count -eq $(state_count completed)
'

test_expect_success 'flux-jobs --format={waitstatus},{returncode}' '
	FORMAT="{waitstatus:h},{returncode:h}" &&
	flux jobs --filter=pending,running -no "$FORMAT" > returncodePR.out &&
	flux jobs --filter=inactive -no "$FORMAT" > returncodeI.out &&
	test_debug "echo active:; cat returncodePR.out" &&
	test_debug "echo inactive:; cat returncodeI.out" &&
	countPR=$(grep -c "^-,-$" returncodePR.out) &&
	test_debug "echo active got $countPR, want $(state_count sched run)" &&
	test $countPR -eq $(state_count sched run) &&
	count=$(grep -c "^32512,127$" returncodeI.out) &&
	test_debug "echo exit 127 got $count, want $(state_count failed)" &&
	test $count -eq $(state_count failed) &&
	count=$(grep -c "^36352,142$" returncodeI.out) &&
	test_debug "echo exit 142 got $count, want $(state_count timeout)" &&
	test $count -eq $(state_count timeout) &&
	count=$(grep -c "^0,0$" returncodeI.out) &&
	test_debug "echo complete got $count, want $(state_count completed)" &&
	test $count -eq $(state_count completed) &&
	count=$(grep -c "^-,-128$" returncodeI.out) &&
	test_debug "echo canceled got $count, want $(state_count canceled)" &&
	test $count -eq $(state_count canceled)
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
	for i in `seq 1 $(state_count active)`; do
		echo ",-,,-,,-" >> invalid-annotations.exp
	done &&
	test_cmp invalid-annotations.out invalid-annotations.exp
'

test_expect_success 'flux-jobs "user" short hands work for job memo' '
       for id in $(state_ids sched); do
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
	fmt="${fmt},{annotations.sched.t_estimate!D:h}" &&
	fmt="${fmt},{annotations.sched.t_estimate!F:h}" &&
	fmt="${fmt},{annotations.sched.t_estimate!H:h}" &&
	flux jobs -no "${fmt}" >t_estimate_annotations.out 2>&1 &&
	test_debug "cat t_estimate_annotations.out" &&
	for i in `seq 1 $(state_count active)`; do
		echo ",00:00,,,,-,-,-" >> t_estimate_annotations.exp
	done &&
	test_cmp t_estimate_annotations.out t_estimate_annotations.exp
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
	id.hex==JOBID
	id.dothex==JOBID
	id.words==JOBID
	userid==UID
	username==USER
	urgency==URG
	priority==PRI
	state==STATE
	state_single==S
	name==NAME
	ntasks==NTASKS
	nnodes==NNODES
	ranks==RANKS
	nodelist==NODELIST
	success==SUCCESS
	exception.occurred==EXCEPTION-OCCURRED
	exception.severity==EXCEPTION-SEVERITY
	exception.type==EXCEPTION-TYPE
	exception.note==EXCEPTION-NOTE
	result==RESULT
	result_abbrev==RS
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

for opt in "" "--color=always" "--color=auto"; do
	test_expect_success "flux-jobs $opt color works (pty)" '
		name=${opt##--color=} &&
		outfile=color-${name:-default}.out &&
		$runpty flux jobs ${opt} --suppress-header -a \
		    | grep -v "version" > $outfile &&
		count=$(no_color_lines $outfile) &&
		test $count -eq $(state_count sched run) &&
		count=$(green_line_count $outfile) &&
		test $count -eq $(state_count completed) &&
		count=$(red_line_count $outfile) &&
		test $count -eq $(state_count failed timeout) &&
		count=$(grey_line_count $outfile) &&
		test $count -eq $(state_count canceled)
	'
done

test_expect_success 'flux-jobs --color=never works (pty)' '
	$runpty flux jobs --suppress-header --color=never -a >color_never.out &&
	check_no_color color_never.out
'

for opt in "" "--color=never"; do
	test_expect_success "flux-jobs $opt color works (no tty)" '
		name=${opt##--color=} &&
		outfile=color-${name:-default}-notty.out &&
		flux jobs ${opt} --suppress-header -a > $outfile &&
		test_must_fail grep "" $outfile
	'
done

test_expect_success 'flux-jobs: --color=always works (notty)' '
	flux jobs --color=always --suppress-header -a > color-always-notty.out &&
	grep "" color-always-notty.out
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

test_expect_success HAVE_JQ 'flux-jobs reverts username to userid for invalid ids' '
	id=$(find_invalid_userid) &&
	test_debug "echo first invalid userid is ${id}" &&
	printf "%s\n" $id > invalid_userid.expected &&
	flux job list -a -c 1 | $jq -c ".userid = ${id}" |
	  flux jobs --from-stdin --suppress-header --format="{username}" \
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
test_expect_success 'flux-jobs --stats works' '
	flux jobs --stats -a >stats.output &&
	test_debug "cat stats.output" &&
	fail=$(state_count failed canceled timeout) &&
	run=$(state_count run) &&
	inactive=$(state_count inactive) &&
	active=$(state_count active) &&
	comp=$((inactive - fail)) &&
	pend=$((active - run)) &&
	cat <<-EOF >stats.expected &&
	${run} running, ${comp} completed, ${fail} failed, ${pend} pending
	EOF
	head -1 stats.output > stats.actual &&
	test_cmp stats.expected stats.actual
'

test_expect_success 'flux-jobs --stats-only works' '
	flux jobs --stats-only > stats-only.output &&
	test_cmp stats.expected stats-only.output
'

test_expect_success 'cleanup job listing jobs ' '
        for jobid in `cat active.ids`; do \
            flux job cancel $jobid; \
            fj_wait_event $jobid clean; \
        done
'

#
# invalid job data tests
#
# note that these tests should be done last, as the introduction of
# invalid job data into the KVS could affect tests above.
#

# Following tests use invalid jobspecs, must load a more permissive validator

ingest_module ()
{
        cmd=$1; shift
        flux module ${cmd} job-ingest $*
}

test_expect_success 'reload job-ingest without validator' '
        ingest_module reload disable-validator
'

test_expect_success HAVE_JQ 'create illegal jobspec with empty command array' '
        cat hostname.json | $jq ".tasks[0].command = []" > bad_jobspec.json
'

# to avoid potential racyness, wait up to 5 seconds for job to appear
# in job list.  note that ntasks will not be set if jobspec invalid
test_expect_success HAVE_JQ 'flux jobs works on job with illegal jobspec' '
        jobid=`flux job submit bad_jobspec.json` &&
        fj_wait_event $jobid clean &&
        i=0 &&
        while ! flux jobs --filter=inactive | grep $jobid > /dev/null \
               && [ $i -lt 5 ]
        do
                sleep 1
                i=$((i + 1))
        done &&
        test "$i" -lt "5" &&
        flux jobs -no "{name},{ntasks}" $jobid > list_illegal_jobspec.out &&
        echo "," > list_illegal_jobspec.exp &&
        test_cmp list_illegal_jobspec.out list_illegal_jobspec.exp
'

test_expect_success 'reload job-ingest with defaults' '
        ingest_module reload
'

# we make R invalid by overwriting it in the KVS before job-list will
# look it up
test_expect_success HAVE_JQ 'flux jobs works on job with illegal R' '
	${RPC} job-list.job-state-pause 0 </dev/null &&
        jobid=`flux mini submit --wait hostname` &&
        jobkvspath=`flux job id --to kvs $jobid` &&
        flux kvs put "${jobkvspath}.R=foobar" &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
        i=0 &&
        while ! flux jobs --filter=inactive | grep $jobid > /dev/null \
               && [ $i -lt 5 ]
        do
                sleep 1
                i=$((i + 1))
        done &&
        test "$i" -lt "5" &&
        flux jobs -no "{ranks},{nnodes},{nodelist},{expiration}" $jobid \
            > list_illegal_R.out &&
        echo ",,,0.0" > list_illegal_R.exp &&
        test_cmp list_illegal_R.out list_illegal_R.exp
'

#
# special tests
#

# use flux queue to ensure jobs stay in pending state
test_expect_success HAVE_JQ 'flux jobs lists nnodes for pending jobs correctly' '
	flux queue stop &&
	id1=$(flux mini submit -N1 hostname) &&
	id2=$(flux mini submit -N3 hostname) &&
	flux jobs -no "{state},{nnodes},{nnodes:h}" ${id1} ${id2}> nnodesP.out &&
	echo "SCHED,1,1" >> nnodesP.exp &&
	echo "SCHED,3,3" >> nnodesP.exp &&
	flux job cancel ${id1} &&
	flux job cancel ${id2} &&
	flux queue start &&
	test_cmp nnodesP.exp nnodesP.out
'

# over subscribe tasks onto nodes through workaround, ensure
# ntasks is larger than the tasks specified via -n option
test_expect_success 'flux jobs lists ntasks with per-resource type=node correctly' '
	nnodes=$(flux resource list -s up -no {nnodes}) &&
	ncores=$(flux resource list -s up -no {ncores}) &&
	extra=$((ncores / nnodes + 2)) &&
	id=$(flux mini submit -N ${nnodes} -n ${ncores} \
	        -o per-resource.type=node \
	        -o per-resource.count=${extra} \
	        /bin/true) &&
	fj_wait_event ${id} clean &&
	flux jobs -no "{ntasks}" ${id} > per_resource_ntasks.out &&
        test $(cat per_resource_ntasks.out) -eq $((nnodes * extra))
'

#
# leave job cleanup to rc3
#
test_done
