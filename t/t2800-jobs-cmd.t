#!/bin/sh

test_description='Test flux jobs command'

. $(dirname $0)/sharness.sh

#  Set path to jq(1)
#
jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ

test_under_flux 4 job

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'load job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux exec -r all flux module load barrier &&
        flux module load sched-simple &&
        flux module load job-exec
'

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
# until all of the pending jobs have rached SCHED state.
#

wait_sched() {
        local i=0
        while [ "$(flux jobs | grep SCHED | wc -l)" != "6" ]\
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
        wait_sched
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

# TODO: need to submit jobs as another user and test -A again
test_expect_success 'flux-jobs -a and -A works' '
        count=`flux jobs --suppress-header -a | wc -l` &&
        test $count -eq 18 &&
        count=`flux jobs --suppress-header -a | wc -l` &&
        test $count -eq 18
'

test_expect_success 'flux-jobs --states works' '
        count=`flux jobs --suppress-header --states=pending | wc -l` &&
        test $count -eq 6 &&
        count=`flux jobs --suppress-header --states=running | wc -l` &&
        test $count -eq 8 &&
        count=`flux jobs --suppress-header --states=inactive | wc -l` &&
        test $count -eq 4 &&
        count=`flux jobs --suppress-header --states=pending,running | wc -l` &&
        test $count -eq 14 &&
        count=`flux jobs --suppress-header --states=pending,inactive | wc -l` &&
        test $count -eq 10 &&
        count=`flux jobs --suppress-header --states=running,inactive | wc -l` &&
        test $count -eq 12 &&
        count=`flux jobs --suppress-header --states=pending,running,inactive | wc -l` &&
        test $count -eq 18
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
        test $count -eq 14 &&
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
        flux jobs --suppress-header --state=running,inactive --format="{t_run}" > t_runRI.out &&
        count=`cat t_runP.out | grep "^0.0$" | wc -l` &&
        test $count -eq 6 &&
        count=`cat t_runRI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=pending,running --format="{t_cleanup}" > t_cleanupPR.out &&
        flux jobs --suppress-header --state=inactive --format="{t_cleanup}" > t_cleanupI.out &&
        count=`cat t_cleanupPR.out | grep "^0.0$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_cleanupI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 4 &&
        flux jobs --suppress-header --state=pending,running --format="{t_inactive}" > t_inactivePR.out &&
        flux jobs --suppress-header --state=inactive --format="{t_inactive}" > t_inactiveI.out &&
        count=`cat t_inactivePR.out | grep "^0.0$" | wc -l` &&
        test $count -eq 14 &&
        count=`cat t_inactiveI.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 4
'

test_expect_success 'flux-jobs --format={runtime},{runtime_fsd},{runtime_fsd_hyphen},{runtime_hms} works' '
        flux jobs --suppress-header --state=pending --format="{runtime},{runtime_fsd},{runtime_fsd_hyphen},{runtime_hms}" > runtimeP.out &&
        for i in `seq 1 6`; do echo "0.0,0s,-,0:00:00" >> runtimeP.exp; done &&
        test_cmp runtimeP.out runtimeP.exp &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime}" > runtimeRI_1.out &&
        count=`cat runtimeRI_1.out | grep -v "^0.0$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_fsd}" > runtimeRI_2.out &&
        count=`cat runtimeRI_2.out | grep -v "^0s" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_fsd_hyphen}" > runtimeRI_3.out &&
        count=`cat runtimeRI_3.out | grep -v "^-$" | wc -l` &&
        test $count -eq 12 &&
        flux jobs --suppress-header --state=running,inactive --format="{runtime_hms}" > runtimeRI_4.out &&
        count=`cat runtimeRI_4.out | grep -v "^0:00:00" | wc -l` &&
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
		flux jobs --from-stdin ${fmt:+--format="$fmt"} < ${d}/input >${issue}.output &&
		test_cmp ${d}/output ${issue}.output
	'
done

#
# cleanup
#
test_expect_success 'cleanup job listing jobs ' '
        for jobid in `cat job_ids_pending.out`; do \
            flux job cancel $jobid; \
            flux job wait-event $jobid clean; \
        done &&
        for jobid in `cat job_ids_running.out`; do \
            flux job cancel $jobid; \
            flux job wait-event $jobid clean; \
        done
'

test_expect_success 'remove sched-simple,job-exec modules' '
        flux exec -r all flux module remove barrier &&
        flux module remove sched-simple &&
        flux module remove job-exec
'

test_done
