#!/bin/sh

test_description='Test flux job manager service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi
if flux job submit --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
fi

test_under_flux 4 kvs

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J=${JOBSPEC}/y2j.py

test_expect_success 'flux-job: convert basic.yaml to JSON' '
        ${Y2J} <${JOBSPEC}/valid/basic.yaml >basic.json
'

test_expect_success 'job-manager: load job-ingest, job-manager' '
	flux module load -r all job-ingest &&
	flux module load -r 0 job-manager
'

test_expect_success 'job-manager: submit one job' '
	flux job submit basic.json >submit1.out
'

test_expect_success 'job-manager: queue contains 1 job' '
	flux job list -s >list1.out &&
	test $(wc -l <list1.out) -eq 1
'

test_expect_success 'job-manager: queue lists job with correct jobid' '
	cut -f1 <list1.out >list1_jobid.out &&
	test_cmp submit1.out list1_jobid.out
'

test_expect_success 'job-manager: queue lists job with correct userid' '
	id -u >list1_userid.exp &&
	cut -f2 <list1.out >list1_userid.out &&
	test_cmp list1_userid.exp list1_userid.out
'

test_expect_success 'job-manager: queue list job with correct priority' '
	echo 16 >list1_priority.exp &&
	cut -f3 <list1.out >list1_priority.out &&
	test_cmp list1_priority.exp list1_priority.out
'

test_expect_success 'job-manager: cancel --purge job' '
	flux job cancel --purge $(cat list1_jobid.out)
'

test_expect_success 'job-manager: queue contains 0 jobs' '
	test $(flux job list -s | wc -l) -eq 0
'

test_expect_success 'job-manager: submit jobs with priority=min,default,max' '
	flux job submit -p0  basic.json >submit_min.out &&
	flux job submit      basic.json >submit_def.out &&
	flux job submit -p31 basic.json >submit_max.out
'

test_expect_success 'job-manager: queue contains 3 jobs' '
	flux job list -s >list3.out &&
	test $(wc -l <list3.out) -eq 3
'

test_expect_success 'job-manager: queue is sorted in priority order' '
	cat >list3_pri.exp <<-EOT
	31
	16
	0
	EOT &&
	cut -f3 <list3.out >list3_pri.out &&
	test_cmp list3_pri.exp list3_pri.out
'

test_expect_success 'job-manager: flux job list --count shows highest priority jobs' '
	cat >list3_lim2.exp <<-EOT
	31
	16
	EOT &&
	flux job list -s -c 2 >list3_lim2.out &&
	test_cmp list3_lim2.exp list3_lim2.out
'

test_expect_success 'job-manager: cancel --purge jobs' '
	for jobid in $(cut -f1 <list3.out); do \
		flux job cancel --purge ${jobid}; \
	done
'

test_expect_success 'job-manager: queue contains 0 jobs' '
       test $(flux job list -s | wc -l) -eq 0
'

test_expect_success 'job-manager: submit 10 jobs of equal priority' '
	rm -f submit10.out &&
	for i in $(seq 1 10); do \
	    flux job submit basic.json >>submit10.out; \
	done
'

test_expect_success 'job-manager: jobs are listed in submit order' '
	flux job list -s >list10.out &&
	cut -f1 <list10.out >list10_ids.out &&
	test_cmp submit10.out list10_ids.out
'

test_expect_success 'job-manager: flux job priority sets last job priority=31' '
	lastid=$(tail -1 <list10_ids.out) &&
	flux job priority ${lastid} 31
'

test_expect_success 'job-manager: priority was updated in KVS' '
	jobid=$(tail -1 <list10_ids.out) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs get ${kvsdir}.priority >pri.out &&
	echo 31 >pri.exp &&
	test_cmp pri.exp pri.out
'

test_expect_success 'job-manager: that job is now the first job' '
	flux job list -s >list10_reordered.out &&
	firstid=$(cut -f1 <list10_reordered.out | head -1) &&
	lastid=$(tail -1 <list10_ids.out) &&
	test "${lastid}" -eq "${firstid}"
'

test_expect_success 'job-manager: reload the job manager' '
	flux module remove job-manager &&
	flux module load job-manager
'

test_expect_success 'job-manager: queue was successfully reconstructed' '
	flux job list -s >list_reload.out &&
	test_cmp list10_reordered.out list_reload.out
'

test_expect_success 'job-manager: cancel --purge jobs' '
	for jobid in $(cut -f1 <list_reload.out); do \
		flux job cancel --purge ${jobid}; \
	done &&
	test $(flux job list -s | wc -l) -eq 0
'

test_expect_success 'job-manager: flux job priority fails on invalid priority' '
	jobid=$(flux job submit basic.json) &&
	flux job priority ${jobid} 31 &&
	test_must_fail flux job priority ${jobid} -1 &&
	test_must_fail flux job priority ${jobid} 32 &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: guest can reduce priority from default' '
	jobid=$(flux job submit  basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 5 &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: guest can increase to default' '
	jobid=$(flux job submit -p 0 basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 16 &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: guest cannot increase past default' '
	jobid=$(flux job submit basic.json) &&
	! FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 17 &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: guest can decrease from from >default' '
	jobid=$(flux job submit -p 31 basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 17 &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: guest cannot set priority of others jobs' '
	jobid=$(flux job submit basic.json) &&
	newid=$(($(id -u)+1)) &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${newid} \
		flux job priority ${jobid} 0 &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: guest cannot cancel --purge others jobs' '
	jobid=$(flux job submit basic.json) &&
	newid=$(($(id -u)+1)) &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${newid} \
		flux job cancel --purge ${jobid} &&
	flux job cancel --purge ${jobid}
'

test_expect_success 'job-manager: no jobs in the queue' '
	test $(flux job list -s | wc -l) -eq 0
'

test_expect_success 'job-manager: job cancellation logged to eventlog ' '
	jobid=$(flux job submit <basic.json) &&
	kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux job cancel ${jobid} &&
	flux kvs eventlog get ${kvsdir}.eventlog | grep cancel
'

test_expect_success 'job-manager: no jobs in the queue' '
	test $(flux job list -s | wc -l) -eq 0
'

test_expect_success 'job-manager: reload the job manager' '
	flux module remove job-manager &&
	flux module load job-manager
'

test_expect_success 'job-manager: still no jobs in the queue' '
	test $(flux job list -s | wc -l) -eq 0
'

test_expect_success 'job-manager: remove job-manager, job-ingest' '
	flux module remove -r 0 job-manager && \
	flux module remove -r all job-ingest
'

test_done
