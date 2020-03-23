#!/bin/sh

test_description='Test flux jobs command'

. $(dirname $0)/sharness.sh

#  Set path to jq(1)
#
jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ

test_under_flux 4 job

# submit a whole bunch of jobs for job list testing
#
# - the first loop of job submissions are intended to have some jobs run
#   quickly and complete
# - the second loop of job submissions are intended to eat up all resources
# - the last job submissions are intended to get a create a set of
#   pending jobs, because jobs from the second loop have taken all resources
# - job ids are stored in files in the order we expect them to be listed
#   - pending jobs - by priority (highest first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#
# the job-info module has eventual consistency with the jobs stored in
# the job-manager's queue.  To ensure no raciness in tests, we spin
# until all of the pending jobs have reached SCHED state, running jobs
# have reached RUN state, and inactive jobs have reached INACTIVE
# state.
#

wait_states() {
        local i=0
        while ( [ "$(flux jobs --suppress-header --states=sched | wc -l)" != "6" ] \
                || [ "$(flux jobs --suppress-header --states=run | wc -l)" != "8" ] \
                || [ "$(flux jobs --suppress-header --states=inactive | wc -l)" != "4" ]) \
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

test_expect_success 'submit jobs for job list testing' '
        for i in `seq 1 4`; do \
            jobid=`flux mini submit hostname`; \
            flux job wait-event $jobid clean; \
            echo $jobid >> job_ids1.out; \
        done &&
        tac job_ids1.out > job_ids_inactive.out &&
        for i in `seq 1 8`; do \
            jobid=`flux mini submit sleep 600`; \
            flux job wait-event $jobid start; \
            echo $jobid >> job_ids2.out; \
        done &&
        tac job_ids2.out > job_ids_running.out &&
        flux mini submit --priority=30 sleep 600 >> job_ids_pending.out &&
        flux mini submit --priority=25 sleep 600 >> job_ids_pending.out &&
        flux mini submit --priority=20 sleep 600 >> job_ids_pending.out &&
        flux mini submit --priority=15 sleep 600 >> job_ids_pending.out &&
        flux mini submit --priority=10 sleep 600 >> job_ids_pending.out &&
        flux mini submit --priority=5 sleep 600 >> job_ids_pending.out &&
        wait_states
'

#
# basic tests
#

# careful with counting b/c of header
test_expect_success 'flux-jobs default output works' '
        count=`flux jobs | wc -l` &&
        test $count -eq 15 &&
        count=`flux jobs | grep " SCHED " | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs | grep " RUN " | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs | grep " INACTIVE " | wc -l` &&
        test $count -eq 0
'

test_expect_success 'flux-jobs --suppress-header works' '
        count=`flux jobs --suppress-header | wc -l` &&
        test $count -eq 14
'

test_expect_success 'flux-jobs: header included with custom formats' '
	flux jobs --format={id} &&
	test "$(flux jobs --format={id} | head -1)" = "JOBID"
'

test_expect_success 'flux-jobs: custom format with numeric spec works' '
	flux jobs --format="{t_run:12.2f}" > format-test.out 2>&1 &&
	test_debug "cat format-test.out" &&
	grep T_RUN format-test.out
'

# TODO: need to submit jobs as another user and test -A again
test_expect_success 'flux-jobs -a and -A works' '
        count=`flux jobs --suppress-header -a | wc -l` &&
        test $count -eq 18 &&
        count=`flux jobs --suppress-header -a | wc -l` &&
        test $count -eq 18
'

# Recall pending = depend & sched, running = run & cleanup,
#  active = pending & running
test_expect_success 'flux-jobs --states works' '
        count=`flux jobs --suppress-header --states=depend | wc -l` &&
        test $count -eq 0 &&
        count=`flux jobs --suppress-header --states=sched | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs --suppress-header --states=pending | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs --suppress-header --states=run | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs --suppress-header --states=cleanup | wc -l` &&
        test $count -eq 0 &&
        count=`flux jobs --suppress-header --states=running | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs --suppress-header --states=inactive | wc -l` &&
        test $count -eq 4 &&
        count=`flux jobs --suppress-header --states=pending,running | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --states=sched,run | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --states=active | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --states=depend,sched,run,cleanup | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --states=pending,inactive | wc -l` &&
        test $count -eq 10 &&
        count=`flux jobs --suppress-header --states=sched,inactive | wc -l` &&
        test $count -eq 10 &&
        count=`flux jobs --suppress-header --states=running,inactive | wc -l` &&
        test $count -eq 12 &&
        count=`flux jobs --suppress-header --states=run,inactive | wc -l` &&
        test $count -eq 12 &&
        count=`flux jobs --suppress-header --states=pending,running,inactive | wc -l` &&
        test $count -eq 18 &&
        count=`flux jobs --suppress-header --states=active,inactive | wc -l` &&
        test $count -eq 18 &&
        count=`flux jobs --suppress-header --states=depend,cleanup | wc -l` &&
        test $count -eq 0
'

test_expect_success 'flux-jobs --states with invalid state fails' '
        test_must_fail flux jobs --states=foobar 2> invalidstate.err &&
        grep "Invalid state specified: foobar" invalidstate.err
'

# ensure + prefix works
# increment userid to ensure not current user for test
test_expect_success 'flux-jobs --user=UID works' '
        userid=`id -u` &&
        count=`flux jobs --suppress-header --user=${userid} | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --user="+${userid}" | wc -l` &&
        test $count -eq 14 &&
        userid=$((userid+1)) &&
        count=`flux jobs --suppress-header --user=${userid} | wc -l` &&
        test $count -eq 0
'

test_expect_success 'flux-jobs --user=USERNAME works' '
        username=`whoami` &&
        count=`flux jobs --suppress-header --user=${username} | wc -l` &&
        test $count -eq 14
'

test_expect_success 'flux-jobs --user with invalid username fails' '
        username="foobarfoobaz" &&
        test_must_fail flux jobs --suppress-header --user=${username}
'

test_expect_success 'flux-jobs --user=all works' '
        count=`flux jobs --suppress-header --user=all | wc -l` &&
        test $count -eq 14
'

test_expect_success 'flux-jobs --count works' '
        count=`flux jobs --suppress-header -a --count=0 | wc -l` &&
        test $count -eq 18 &&
        count=`flux jobs --suppress-header -a --count=8 | wc -l` &&
        test $count -eq 8
'

#
# format tests
#

test_expect_success 'flux-jobs --format={id} works' '
        flux jobs --suppress-header --state=pending --format="{id}" > idsP.out &&
        test_cmp idsP.out job_ids_pending.out &&
        flux jobs --suppress-header --state=running --format="{id}" > idsR.out &&
        test_cmp idsR.out job_ids_running.out &&
        flux jobs --suppress-header --state=inactive --format="{id}" > idsI.out &&
        test_cmp idsI.out job_ids_inactive.out
'

test_expect_success 'flux-jobs --format={userid},{username} works' '
        flux jobs --suppress-header -a --format="{userid},{username}" > user.out &&
        id=`id -u` &&
        name=`whoami` &&
        for i in `seq 1 18`; do echo "${id},${name}" >> user.exp; done &&
        test_cmp user.out user.exp
'

test_expect_success 'flux-jobs --format={state},{state_single} works' '
        flux jobs --suppress-header --state=pending --format="{state},{state_single}" > stateP.out &&
        for i in `seq 1 6`; do echo "SCHED,S" >> stateP.exp; done &&
        test_cmp stateP.out stateP.exp &&
        flux jobs --suppress-header --state=running --format="{state},{state_single}" > stateR.out &&
        for i in `seq 1 8`; do echo "RUN,R" >> stateR.exp; done &&
        test_cmp stateR.out stateR.exp &&
        flux jobs --suppress-header --state=inactive --format="{state},{state_single}" > stateI.out &&
        for i in `seq 1 4`; do echo "INACTIVE,I" >> stateI.exp; done &&
        test_cmp stateI.out stateI.exp
'

test_expect_success 'flux-jobs --format={name} works' '
        flux jobs --suppress-header --state=pending,running --format="{name}" > jobnamePR.out &&
        for i in `seq 1 14`; do echo "sleep" >> jobnamePR.exp; done &&
        test_cmp jobnamePR.out jobnamePR.exp &&
        flux jobs --suppress-header --state=inactive --format="{name}" > jobnameI.out &&
        for i in `seq 1 4`; do echo "hostname" >> jobnameI.exp; done &&
        test_cmp jobnameI.out jobnameI.exp
'

test_expect_success 'flux-jobs --format={ntasks} works' '
        flux jobs --suppress-header -a --format="{ntasks}" > taskcount.out &&
        for i in `seq 1 18`; do echo "1" >> taskcount.exp; done &&
        test_cmp taskcount.out taskcount.exp
'

test_expect_success 'flux-jobs --format={nnodes},{nnodes:h} works' '
        flux jobs --suppress-header --state=pending --format="{nnodes},{nnodes:h}" > nodecountP.out &&
        for i in `seq 1 6`; do echo ",-" >> nodecountP.exp; done &&
        test_cmp nodecountP.out nodecountP.exp &&
        flux jobs --suppress-header --state=running,inactive --format="{nnodes},{nnodes:h}" > nodecountRI.out &&
        for i in `seq 1 12`; do echo "1,1" >> nodecountRI.exp; done &&
        test_cmp nodecountRI.out nodecountRI.exp
'

test_expect_success 'flux-jobs --format={runtime:0.3f} works' '
        flux jobs --suppress-header --state=pending --format="{runtime:0.3f}" > runtime-dotP.out &&
        for i in `seq 1 6`; do echo "0.000" >> runtime-dotP.exp; done &&
        test_cmp runtime-dotP.out runtime-dotP.exp &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime:0.3f}" > runtime-dotRI.out &&
        [ "$(grep -E "\.[0-9]{3}" runtime-dotRI.out | wc -l)" = "12" ]
'

test_expect_success 'flux-jobs --format={runtime:0.3f} works with header' '
        flux jobs --state=pending --format="{runtime:0.3f}" > runtime-header.out &&
        echo "RUNTIME" >> runtime-header.exp &&
        for i in `seq 1 6`; do echo "0.000" >> runtime-header.exp; done &&
        test_cmp runtime-header.out runtime-header.exp
'

test_expect_success 'flux-jobs --format={id:d} works with header' '
        flux jobs --state=pending --format="{id:d}" > id-decimal.out &&
        [ "$(grep -E "^[0-9]+$" id-decimal.out | wc -l)" = "6" ]
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


# node ranks assumes sched-simple default of mode='worst-fit'
test_expect_success 'flux-jobs --format={ranks},{ranks:h} works' '
        flux jobs --suppress-header --state=pending --format="{ranks},{ranks:h}" > ranksP.out &&
        for i in `seq 1 6`; do echo ",-" >> ranksP.exp; done &&
        test_cmp ranksP.out ranksP.exp &&
        flux jobs --suppress-header --state=running --format="{ranks},{ranks:h}" > ranksR.out &&
        for i in `seq 1 2`; \
        do \
            echo "3,3" >> ranksR.exp; \
            echo "2,2" >> ranksR.exp; \
            echo "1,1" >> ranksR.exp; \
            echo "0,0" >> ranksR.exp; \
        done &&
        test_cmp ranksR.out ranksR.exp &&
        flux jobs --suppress-header --state=inactive --format="{ranks},{ranks:h}" > ranksI.out &&
        for i in `seq 1 4`; do echo "0,0" >> ranksI.exp; done &&
        test_cmp ranksI.out ranksI.exp
'

# test just make sure numbers are zero or non-zero given state of job
test_expect_success 'flux-jobs --format={t_XXX} works' '
        flux jobs --suppress-header -a --format="{t_submit}" > t_submit.out &&
        count=`cat t_submit.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 18 &&
        flux jobs --suppress-header -a --format="{t_depend}" > t_depend.out &&
        count=`cat t_depend.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 18 &&
        flux jobs --suppress-header -a --format="{t_sched}" > t_sched.out &&
        count=`cat t_sched.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 18 &&
        flux jobs --suppress-header --state=pending --format="{t_run}" > t_runP.out &&
        flux jobs --suppress-header --state=pending --format="{t_run:h}" > t_runP_h.out &&
        flux jobs --suppress-header --state=running,inactive --format="{t_run}" > t_runRI.out &&
        count=`cat t_runP.out | grep "^0.0$" | wc -l` &&
        test $count -eq 6 &&
        count=`cat t_runP_h.out | grep "^-$" | wc -l` &&
        test $count -eq 6 &&
        count=`cat t_runRI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=pending,running --format="{t_cleanup}" > t_cleanupPR.out &&
        flux jobs --suppress-header --state=pending,running --format="{t_cleanup:h}" > t_cleanupPR_h.out &&
        flux jobs --suppress-header --state=inactive --format="{t_cleanup}" > t_cleanupI.out &&
        count=`cat t_cleanupPR.out | grep "^0.0$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_cleanupPR_h.out | grep "^-$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 4 &&
        flux jobs --suppress-header --state=pending,running --format="{t_inactive}" > t_inactivePR.out &&
        flux jobs --suppress-header --state=pending,running --format="{t_inactive:h}" > t_inactivePR_h.out &&
        flux jobs --suppress-header --state=inactive --format="{t_inactive}" > t_inactiveI.out &&
        count=`cat t_inactivePR.out | grep "^0.0$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_inactivePR_h.out | grep "^-$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_inactiveI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 4
'

test_expect_success 'flux-jobs --format={runtime},{runtime_fsd},{runtime_fsd:h},{runtime_hms},{runtime_hms:h} works' '
        flux jobs --suppress-header --state=pending --format="{runtime},{runtime_fsd},{runtime_hms}" > runtimeP.out &&
        for i in `seq 1 6`; do echo "0.0,0s,0:00:00" >> runtimeP.exp; done &&
        test_cmp runtimeP.out runtimeP.exp &&
        flux jobs --suppress-header --state=pending --format="{runtime_fsd:h},{runtime_hms:h}" > runtimeP_h.out &&
        for i in `seq 1 6`; do echo "-,-" >> runtimeP_h.exp; done &&
        test_cmp runtimeP_h.out runtimeP_h.exp &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime}" > runtimeRI_1.out &&
        count=`cat runtimeRI_1.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime:h}" > runtimeRI_1_h.out &&
        count=`cat runtimeRI_1_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_fsd}" > runtimeRI_2.out &&
        count=`cat runtimeRI_2.out | grep -v "^0s" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_fsd:h}" > runtimeRI_2_h.out &&
        count=`cat runtimeRI_2_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_hms}" > runtimeRI_3.out &&
        count=`cat runtimeRI_3.out | grep -v "^0:00:00$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_hms:h}" > runtimeRI_3_h.out &&
        count=`cat runtimeRI_3_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 12
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

test_expect_success 'flux-jobs --from-stdin works with no input' '
	flux jobs --from-stdin </dev/null
'

test_expect_success 'flux-jobs --from-stdin fails with invalid input' '
	echo foo | test_must_fail flux jobs --from-stdin
'

find_invalid_userid() {
	python -c 'import pwd; \
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

#
# leave job cleanup to rc3
#
test_done
