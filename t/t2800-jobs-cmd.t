#!/bin/sh

test_description='Test flux jobs command'

. $(dirname $0)/sharness.sh

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
        while ( [ "$(flux jobs --suppress-header --filter=sched | wc -l)" != "6" ] \
                || [ "$(flux jobs --suppress-header --filter=run | wc -l)" != "8" ] \
                || [ "$(flux jobs --suppress-header --filter=inactive | wc -l)" != "6" ]) \
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
        jobid=`flux mini submit nosuchcommand` &&
        flux job wait-event $jobid clean &&
        echo $jobid >> job_ids1.out &&
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
        jobid=`flux mini submit cancelledjob` &&
        flux job wait-event $jobid depend &&
        flux job cancel $jobid &&
        flux job wait-event $jobid clean &&
        echo $jobid >> job_ids1.out &&
        tac job_ids1.out > job_ids_inactive.out &&
        wait_states
'

#
# basic tests
#

# careful with counting b/c of header
test_expect_success 'flux-jobs default output works' '
        count=`flux jobs | wc -l` &&
        test $count -eq 15 &&
        count=`flux jobs | grep "    PD " | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs | grep "     R " | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs | grep "    CD " | wc -l` &&
        test $count -eq 0 &&
        count=`flux jobs | grep "    CA " | wc -l` &&
        test $count -eq 0 &&
        count=`flux jobs | grep "     F " | wc -l` &&
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
        test $count -eq 20 &&
        count=`flux jobs --suppress-header -a | wc -l` &&
        test $count -eq 20
'

# Recall pending = depend & sched, running = run & cleanup,
#  active = pending & running
test_expect_success 'flux-jobs --filter works' '
        count=`flux jobs --suppress-header --filter=depend | wc -l` &&
        test $count -eq 0 &&
        count=`flux jobs --suppress-header --filter=sched | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs --suppress-header --filter=pending | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs --suppress-header --filter=run | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs --suppress-header --filter=cleanup | wc -l` &&
        test $count -eq 0 &&
        count=`flux jobs --suppress-header --filter=running | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs --suppress-header --filter=inactive | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs --suppress-header --filter=pending,running | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --filter=sched,run | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --filter=active | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --filter=depend,sched,run,cleanup | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --filter=pending,inactive | wc -l` &&
        test $count -eq 12 &&
        count=`flux jobs --suppress-header --filter=sched,inactive | wc -l` &&
        test $count -eq 12 &&
        count=`flux jobs --suppress-header --filter=running,inactive | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --filter=run,inactive | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --filter=pending,running,inactive | wc -l` &&
        test $count -eq 20 &&
        count=`flux jobs --suppress-header --filter=active,inactive | wc -l` &&
        test $count -eq 20 &&
        count=`flux jobs --suppress-header --filter=depend,cleanup | wc -l` &&
        test $count -eq 0
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
        test $count -eq 20 &&
        count=`flux jobs --suppress-header -a --count=8 | wc -l` &&
        test $count -eq 8
'

#
# test specific IDs
#

test_expect_success 'flux-jobs specific IDs works' '
        ids=`cat job_ids_pending.out` &&
        count=`flux jobs --suppress-header ${ids} | wc -l` &&
        test $count -eq 6 &&
        ids=`cat job_ids_running.out` &&
        count=`flux jobs --suppress-header ${ids} | wc -l` &&
        test $count -eq 8 &&
        ids=`cat job_ids_inactive.out` &&
        count=`flux jobs --suppress-header ${ids} | wc -l` &&
        test $count -eq 6
'

test_expect_success 'flux-jobs error on bad IDs' '
        flux jobs --suppress-header 0 1 2 2> ids.err &&
        count=`grep -i unknown ids.err | wc -l` &&
        test $count -eq 3
'

test_expect_success 'flux-jobs good and bad IDs works' '
        ids=`cat job_ids_pending.out` &&
        flux jobs --suppress-header ${ids} 0 1 2 > ids.out 2> ids.err &&
        count=`wc -l < ids.out` &&
        test $count -eq 6 &&
        count=`grep -i unknown ids.err | wc -l` &&
        test $count -eq 3
'

test_expect_success 'flux-jobs ouputs warning on invalid options' '
        ids=`cat job_ids_pending.out` &&
        flux jobs --suppress-header -A ${ids} > warn.out 2> warn.err &&
        grep WARNING warn.err
'

#
# format tests
#

test_expect_success 'flux-jobs --format={id} works' '
        flux jobs --suppress-header --filter=pending --format="{id}" > idsP.out &&
        test_cmp idsP.out job_ids_pending.out &&
        flux jobs --suppress-header --filter=running --format="{id}" > idsR.out &&
        test_cmp idsR.out job_ids_running.out &&
        flux jobs --suppress-header --filter=inactive --format="{id}" > idsI.out &&
        test_cmp idsI.out job_ids_inactive.out
'

test_expect_success 'flux-jobs --format={userid},{username} works' '
        flux jobs --suppress-header -a --format="{userid},{username}" > user.out &&
        id=`id -u` &&
        name=`whoami` &&
        for i in `seq 1 20`; do echo "${id},${name}" >> user.exp; done &&
        test_cmp user.out user.exp
'

test_expect_success 'flux-jobs --format={state},{state_single} works' '
        flux jobs --suppress-header --filter=pending --format="{state},{state_single}" > stateP.out &&
        for i in `seq 1 6`; do echo "SCHED,S" >> stateP.exp; done &&
        test_cmp stateP.out stateP.exp &&
        flux jobs --suppress-header --filter=running --format="{state},{state_single}" > stateR.out &&
        for i in `seq 1 8`; do echo "RUN,R" >> stateR.exp; done &&
        test_cmp stateR.out stateR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{state},{state_single}" > stateI.out &&
        for i in `seq 1 6`; do echo "INACTIVE,I" >> stateI.exp; done &&
        test_cmp stateI.out stateI.exp
'

test_expect_success 'flux-jobs --format={name} works' '
        flux jobs --suppress-header --filter=pending,running --format="{name}" > jobnamePR.out &&
        for i in `seq 1 14`; do echo "sleep" >> jobnamePR.exp; done &&
        test_cmp jobnamePR.out jobnamePR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{name}" > jobnameI.out &&
        echo "cancelledjob" >> jobnameI.exp &&
        echo "nosuchcommand" >> jobnameI.exp &&
        for i in `seq 1 4`; do echo "hostname" >> jobnameI.exp; done &&
        test_cmp jobnameI.out jobnameI.exp
'

test_expect_success 'flux-jobs --format={ntasks} works' '
        flux jobs --suppress-header -a --format="{ntasks}" > taskcount.out &&
        for i in `seq 1 20`; do echo "1" >> taskcount.exp; done &&
        test_cmp taskcount.out taskcount.exp
'

test_expect_success 'flux-jobs --format={nnodes},{nnodes:h} works' '
        flux jobs --suppress-header --filter=pending --format="{nnodes},{nnodes:h}" > nodecountP.out &&
        for i in `seq 1 6`; do echo ",-" >> nodecountP.exp; done &&
        test_cmp nodecountP.out nodecountP.exp &&
        flux jobs --suppress-header --filter=running --format="{nnodes},{nnodes:h}" > nodecountR.out &&
        for i in `seq 1 8`; do echo "1,1" >> nodecountR.exp; done &&
        test_cmp nodecountR.out nodecountR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{nnodes},{nnodes:h}" > nodecountI.out &&
        echo ",-" >> nodecountI.exp &&
        for i in `seq 1 5`; do echo "1,1" >> nodecountI.exp; done &&
        test_cmp nodecountI.out nodecountI.exp
'

test_expect_success 'flux-jobs --format={runtime:0.3f} works' '
        flux jobs --suppress-header --filter=pending --format="{runtime:0.3f}" > runtime-dotP.out &&
        for i in `seq 1 6`; do echo "0.000" >> runtime-dotP.exp; done &&
        test_cmp runtime-dotP.out runtime-dotP.exp &&
        flux jobs --suppress-header --filter=running,inactive --format="{runtime:0.3f}" > runtime-dotRI.out &&
        [ "$(grep -E "\.[0-9]{3}" runtime-dotRI.out | wc -l)" = "14" ]
'

test_expect_success 'flux-jobs --format={runtime:0.3f} works with header' '
        flux jobs --filter=pending --format="{runtime:0.3f}" > runtime-header.out &&
        echo "RUNTIME" >> runtime-header.exp &&
        for i in `seq 1 6`; do echo "0.000" >> runtime-header.exp; done &&
        test_cmp runtime-header.out runtime-header.exp
'

test_expect_success 'flux-jobs --format={id:d} works with header' '
        flux jobs --filter=pending --format="{id:d}" > id-decimal.out &&
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
        flux jobs --suppress-header --filter=pending --format="{ranks},{ranks:h}" > ranksP.out &&
        for i in `seq 1 6`; do echo ",-" >> ranksP.exp; done &&
        test_cmp ranksP.out ranksP.exp &&
        flux jobs --suppress-header --filter=running --format="{ranks},{ranks:h}" > ranksR.out &&
        for i in `seq 1 2`; \
        do \
            echo "3,3" >> ranksR.exp; \
            echo "2,2" >> ranksR.exp; \
            echo "1,1" >> ranksR.exp; \
            echo "0,0" >> ranksR.exp; \
        done &&
        test_cmp ranksR.out ranksR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{ranks},{ranks:h}" > ranksI.out &&
        echo ",-" >> ranksI.exp &&
        for i in `seq 1 5`; do echo "0,0" >> ranksI.exp; done &&
        test_cmp ranksI.out ranksI.exp
'

# test just make sure numbers are zero or non-zero given state of job
test_expect_success 'flux-jobs --format={t_XXX} works' '
        flux jobs --suppress-header -a --format="{t_submit}" > t_submit.out &&
        count=`cat t_submit.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 20 &&
        flux jobs --suppress-header -a --format="{t_depend}" > t_depend.out &&
        count=`cat t_depend.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 20 &&
        flux jobs --suppress-header -a --format="{t_sched}" > t_sched.out &&
        count=`cat t_sched.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 20 &&
        flux jobs --suppress-header --filter=pending --format="{t_run}" > t_runP.out &&
        flux jobs --suppress-header --filter=pending --format="{t_run:h}" > t_runP_h.out &&
        flux jobs --suppress-header --filter=running --format="{t_run}" > t_runR.out &&
        flux jobs --suppress-header --filter=inactive --format="{t_run}" > t_runI.out &&
        count=`cat t_runP.out | grep "^0.0$" | wc -l` &&
        test $count -eq 6 &&
        count=`cat t_runP_h.out | grep "^-$" | wc -l` &&
        test $count -eq 6 &&
        count=`cat t_runR.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 8 &&
        count=`head -n 1 t_runI.out | grep "^0.0$" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 t_runI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 5 &&
        flux jobs --suppress-header --filter=pending,running --format="{t_cleanup}" > t_cleanupPR.out &&
        flux jobs --suppress-header --filter=pending,running --format="{t_cleanup:h}" > t_cleanupPR_h.out &&
        flux jobs --suppress-header --filter=inactive --format="{t_cleanup}" > t_cleanupI.out &&
        count=`cat t_cleanupPR.out | grep "^0.0$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_cleanupPR_h.out | grep "^-$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 6 &&
        flux jobs --suppress-header --filter=pending,running --format="{t_inactive}" > t_inactivePR.out &&
        flux jobs --suppress-header --filter=pending,running --format="{t_inactive:h}" > t_inactivePR_h.out &&
        flux jobs --suppress-header --filter=inactive --format="{t_inactive}" > t_inactiveI.out &&
        count=`cat t_inactivePR.out | grep "^0.0$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_inactivePR_h.out | grep "^-$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_inactiveI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 6
'

test_expect_success 'flux-jobs --format={runtime},{runtime_fsd},{runtime_fsd:h},{runtime_hms},{runtime_hms:h} works' '
        flux jobs --suppress-header --filter=pending --format="{runtime},{runtime_fsd},{runtime_hms}" > runtimeP.out &&
        for i in `seq 1 6`; do echo "0.0,0s,0:00:00" >> runtimeP.exp; done &&
        test_cmp runtimeP.out runtimeP.exp &&
        flux jobs --suppress-header --filter=pending --format="{runtime_fsd:h},{runtime_hms:h}" > runtimeP_h.out &&
        for i in `seq 1 6`; do echo "-,-" >> runtimeP_h.exp; done &&
        test_cmp runtimeP_h.out runtimeP_h.exp &&
        flux jobs --suppress-header --filter=running --format="{runtime}" > runtimeR_1.out &&
        count=`cat runtimeR_1.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 8 &&
        flux jobs --suppress-header --filter=running --format="{runtime:h}" > runtimeR_1_h.out &&
        count=`cat runtimeR_1_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 8 &&
        flux jobs --suppress-header --filter=running --format="{runtime_fsd}" > runtimeR_2.out &&
        count=`cat runtimeR_2.out | grep -v "^0s" | wc -l` &&
        test $count -eq 8 &&
        flux jobs --suppress-header --filter=running --format="{runtime_fsd:h}" > runtimeR_2_h.out &&
        count=`cat runtimeR_2_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 8 &&
        flux jobs --suppress-header --filter=running --format="{runtime_hms}" > runtimeR_3.out &&
        count=`cat runtimeR_3.out | grep -v "^0:00:00$" | wc -l` &&
        test $count -eq 8 &&
        flux jobs --suppress-header --filter=running --format="{runtime_hms:h}" > runtimeR_3_h.out &&
        count=`cat runtimeR_3_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 8 &&
        flux jobs --suppress-header --filter=inactive --format="{runtime}" > runtimeI_1.out &&
        count=`head -n 1 runtimeI_1.out | grep "^0.0$" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 runtimeI_1.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 5 &&
        flux jobs --suppress-header --filter=inactive --format="{runtime:h}" > runtimeI_1_h.out &&
        count=`head -n 1 runtimeI_1_h.out | grep "^-$" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 runtimeI_1_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 5 &&
        flux jobs --suppress-header --filter=inactive --format="{runtime_fsd}" > runtimeI_2.out &&
        count=`head -n 1 runtimeI_2.out | grep "^0s" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 runtimeI_2.out | grep -v "^0s" | wc -l` &&
        test $count -eq 5 &&
        flux jobs --suppress-header --filter=inactive --format="{runtime_fsd:h}" > runtimeI_2_h.out &&
        count=`head -n 1 runtimeI_2_h.out | grep "^-$" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 runtimeI_2_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 5 &&
        flux jobs --suppress-header --filter=inactive --format="{runtime_hms}" > runtimeI_3.out &&
        count=`head -n 1 runtimeI_3.out | grep "^0:00:00$" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 runtimeI_3.out | grep -v "^0:00:00$" | wc -l` &&
        test $count -eq 5 &&
        flux jobs --suppress-header --filter=inactive --format="{runtime_hms:h}" > runtimeI_3_h.out &&
        count=`head -n 1 runtimeI_3_h.out | grep "^-$" | wc -l` &&
        test $count -eq 1 &&
        count=`tail -n 5 runtimeI_3_h.out | grep -v "^-$" | wc -l` &&
        test $count -eq 5
'

test_expect_success 'flux-jobs --format={success},{success:h} works' '
        flux jobs --suppress-header --filter=pending,running --format="{success},{success:h}" > successPR.out &&
        for i in `seq 1 14`; do echo ",-" >> successPR.exp; done &&
        test_cmp successPR.out successPR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{success},{success:h}" > successI.out &&
        echo "False,False" >> successI.exp &&
        echo "False,False" >> successI.exp &&
        for i in `seq 1 4`; do echo "True,True" >> successI.exp; done &&
        test_cmp successI.out successI.exp
'

test_expect_success 'flux-jobs --format={exception.occurred},{exception.occurred:h} works' '
        flux jobs --suppress-header --filter=pending,running --format="{exception.occurred},{exception.occurred:h}" > exception_occurredPR.out &&
        for i in `seq 1 14`; do echo ",-" >> exception_occurredPR.exp; done &&
        test_cmp exception_occurredPR.out exception_occurredPR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{exception.occurred},{exception.occurred:h}" > exception_occurredI.out &&
        echo "True,True" >> exception_occurredI.exp &&
        echo "True,True" >> exception_occurredI.exp &&
        for i in `seq 1 4`; do echo "False,False" >> exception_occurredI.exp; done &&
        test_cmp exception_occurredI.out exception_occurredI.exp
'

test_expect_success 'flux-jobs --format={exception.severity},{exception.severity:h} works' '
        flux jobs --suppress-header --filter=pending,running --format="{exception.severity},{exception.severity:h}" > exception_severityPR.out &&
        for i in `seq 1 14`; do echo ",-" >> exception_severityPR.exp; done &&
        test_cmp exception_severityPR.out exception_severityPR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{exception.severity},{exception.severity:h}" > exception_severityI.out &&
        echo "0,0" >> exception_severityI.exp &&
        echo "0,0" >> exception_severityI.exp &&
        for i in `seq 1 4`; do echo ",-" >> exception_severityI.exp; done &&
        test_cmp exception_severityI.out exception_severityI.exp
'

test_expect_success 'flux-jobs --format={exception.type},{exception.type:h} works' '
        flux jobs --suppress-header --filter=pending,running --format="{exception.type},{exception.type:h}" > exception_typePR.out &&
        for i in `seq 1 14`; do echo ",-" >> exception_typePR.exp; done &&
        test_cmp exception_typePR.out exception_typePR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{exception.type},{exception.type:h}" > exception_typeI.out &&
        echo "cancel,cancel" >> exception_typeI.exp &&
        echo "exec,exec" >> exception_typeI.exp &&
        for i in `seq 1 4`; do echo ",-" >> exception_typeI.exp; done &&
        test_cmp exception_typeI.out exception_typeI.exp
'

test_expect_success 'flux-jobs --format={exception.note},{exception.note:h} works' '
        flux jobs --suppress-header --filter=pending,running --format="{exception.note},{exception.note:h}" > exception_notePR.out &&
        for i in `seq 1 14`; do echo ",-" >> exception_notePR.exp; done &&
        test_cmp exception_notePR.out exception_notePR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{exception.note},{exception.note:h}" > exception_noteI.out &&
        head -n 1 exception_noteI.out | grep "^,-$" &&
        head -n 2 exception_noteI.out | tail -n 1 | grep "No such file or directory" &&
        tail -n 4 exception_noteI.out > exception_noteI_tail.out &&
        for i in `seq 1 4`; do echo ",-" >> exception_noteI.exp; done &&
        test_cmp exception_noteI_tail.out exception_noteI.exp
'

test_expect_success 'flux-jobs --format={result},{result:h},{result_abbrev},{result_abbrev:h} works' '
        flux jobs --suppress-header --filter=pending,running --format="{result},{result:h}" > resultPR.out &&
        for i in `seq 1 14`; do echo ",-" >> resultPR.exp; done &&
        test_cmp resultPR.out resultPR.exp &&
        flux jobs --suppress-header --filter=pending,running --format="{result_abbrev},{result_abbrev:h}" > result_abbrevPR.out &&
        for i in `seq 1 14`; do echo ",-" >> result_abbrevPR.exp; done &&
        test_cmp result_abbrevPR.out result_abbrevPR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{result},{result:h}" > resultI.out &&
        echo "CANCELLED,CANCELLED" >> resultI.exp &&
        echo "FAILED,FAILED" >> resultI.exp &&
        for i in `seq 1 4`; do echo "COMPLETED,COMPLETED" >> resultI.exp; done &&
        test_cmp resultI.out resultI.exp &&
        flux jobs --suppress-header --filter=inactive --format="{result_abbrev},{result_abbrev:h}" > result_abbrevI.out &&
        echo "CA,CA" >> result_abbrevI.exp &&
        echo "F,F" >> result_abbrevI.exp &&
        for i in `seq 1 4`; do echo "CD,CD" >> result_abbrevI.exp; done &&
        test_cmp result_abbrevI.out result_abbrevI.exp
'

test_expect_success 'flux-jobs --format={status},{status_abbrev} works' '
        flux jobs --suppress-header --filter=pending --format="{status},{status_abbrev}" > statusP.out &&
        for i in `seq 1 6`; do echo "PENDING,PD" >> statusP.exp; done &&
        test_cmp statusP.out statusP.exp &&
        flux jobs --suppress-header --filter=running --format="{status},{status_abbrev}" > statusR.out &&
        for i in `seq 1 8`; do echo "RUNNING,R" >> statusR.exp; done &&
        test_cmp statusR.out statusR.exp &&
        flux jobs --suppress-header --filter=inactive --format="{status},{status_abbrev}" > statusI.out &&
        echo "CANCELLED,CA" >> statusI.exp &&
        echo "FAILED,F" >> statusI.exp &&
        for i in `seq 1 4`; do echo "COMPLETED,CD" >> statusI.exp; done &&
        test_cmp statusI.out statusI.exp
'

#
# format header tests
#
test_expect_success 'flux-jobs: header included with all custom formats' '
	flux jobs --format={id} | head -1 | grep "JOBID" &&
        flux jobs --format={userid} | head -1 | grep "UID" &&
        flux jobs --format={username} | head -1 | grep "USER" &&
        flux jobs --format={priority} | head -1 | grep "PRI" &&
        flux jobs --format={state} | head -1 | grep "STATE" &&
        flux jobs --format={state_single} | head -1 | grep "ST" &&
        flux jobs --format={name} | head -1 | grep "NAME" &&
        flux jobs --format={ntasks} | head -1 | grep "NTASKS" &&
        flux jobs --format={nnodes} | head -1 | grep "NNODES" &&
        flux jobs --format={ranks} | head -1 | grep "RANKS" &&
        flux jobs --format={success} | head -1 | grep "SUCCESS" &&
        flux jobs --format={exception.occurred} | head -1 | grep "EXCEPTION-OCCURRED" &&
        flux jobs --format={exception.severity} | head -1 | grep "EXCEPTION-SEVERITY" &&
        flux jobs --format={exception.type} | head -1 | grep "EXCEPTION-TYPE" &&
        flux jobs --format={exception.note} | head -1 | grep "EXCEPTION-NOTE" &&
        flux jobs --format={result} | head -1 | grep "RESULT" &&
        flux jobs --format={result_abbrev} | head -1 | grep "RS" &&
        flux jobs --format={t_submit} | head -1 | grep "T_SUBMIT" &&
        flux jobs --format={t_depend} | head -1 | grep "T_DEPEND" &&
        flux jobs --format={t_sched} | head -1 | grep "T_SCHED" &&
        flux jobs --format={t_run} | head -1 | grep "T_RUN" &&
        flux jobs --format={t_cleanup} | head -1 | grep "T_CLEANUP" &&
        flux jobs --format={t_inactive} | head -1 | grep "T_INACTIVE" &&
        flux jobs --format={runtime} | head -1 | grep "RUNTIME" &&
        flux jobs --format={runtime_fsd} | head -1 | grep "RUNTIME" &&
        flux jobs --format={runtime_hms} | head -1 | grep "RUNTIME"
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
