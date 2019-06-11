#!/bin/sh

test_description='Test flux job manager service'

. $(dirname $0)/sharness.sh

test_under_flux 4 kvs

flux setattr log-stderr-level 1

DRAIN_UNDRAIN=${FLUX_SOURCE_DIR}/t/job-manager/drain-undrain.py
DRAIN_CANCEL=${FLUX_SOURCE_DIR}/t/job-manager/drain-cancel.py
RPC=${FLUX_BUILD_DIR}/t/request/rpc

# List jobs without header
list_jobs() {
	flux job list -s
}

test_expect_success 'job-manager: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'

test_expect_success 'job-manager: load job-ingest, job-info, job-manager' '
	flux module load -r all job-ingest &&
	flux module load -r all job-info &&
	flux module load -r 0 job-manager
'

test_expect_success 'job-manager: submit one job' '
	flux job submit basic.json >submit1.out
'

test_expect_success 'job-manager: queue contains 1 job' '
	list_jobs >list1.out &&
	test $(wc -l <list1.out) -eq 1
'

test_expect_success 'job-manager: queue lists job with correct jobid' '
	cut -f1 <list1.out >list1_jobid.out &&
	test_cmp submit1.out list1_jobid.out
'

test_expect_success 'job-manager: queue lists job with state=N' '
	echo "S" >list1_state.exp &&
	cut -f2 <list1.out >list1_state.out &&
	test_cmp list1_state.exp list1_state.out
'

test_expect_success 'job-manager: queue lists job with correct userid' '
	id -u >list1_userid.exp &&
	cut -f3 <list1.out >list1_userid.out &&
	test_cmp list1_userid.exp list1_userid.out
'

test_expect_success 'job-manager: queue list job with correct priority' '
	echo 16 >list1_priority.exp &&
	cut -f4 <list1.out >list1_priority.out &&
	test_cmp list1_priority.exp list1_priority.out
'

test_expect_success 'job-manager: raise non-fatal exception on job' '
	jobid=$(cat list1_jobid.out) &&
	flux job raise --severity=1 --type=testing ${jobid} Mumble grumble &&
        flux job wait-event --timeout=5.0 --match-context=type=testing ${jobid} exception &&
	flux job eventlog $jobid \
		| grep exception >list1_exception.out &&
	grep -q "type=\"testing\"" list1_exception.out &&
	grep -q severity=1 list1_exception.out &&
	grep -q "Mumble grumble" list1_exception.out
'

test_expect_success 'job-manager: exception note with embedded = is rejected' '
	jobid=$(cat list1_jobid.out) &&
	! flux job raise --severity=1 --type=testing ${jobid} foo=bar
'

test_expect_success 'job-manager: queue contains 1 jobs' '
	test $(list_jobs | wc -l) -eq 1
'

test_expect_success 'job-manager: cancel job' '
	jobid=$(cat list1_jobid.out) &&
	flux job cancel ${jobid} &&
        flux job wait-event --timeout=5.0 --match-context=type=cancel ${jobid} exception &&
	flux job eventlog $jobid | grep exception \
		| grep severity=0 | grep "type=\"cancel\""
'

test_expect_success 'job-manager: queue contains 0 jobs' '
	test $(list_jobs | wc -l) -eq 0
'

test_expect_success 'job-manager: submit jobs with priority=min,default,max' '
	flux job submit -p0  basic.json >submit_min.out &&
	flux job submit      basic.json >submit_def.out &&
	flux job submit -p31 basic.json >submit_max.out
'

test_expect_success 'job-manager: queue contains 3 jobs' '
	list_jobs >list3.out &&
	test $(wc -l <list3.out) -eq 3
'

test_expect_success 'job-manager: queue is sorted in priority order' '
	cat >list3_pri.exp <<-EOT
	31
	16
	0
	EOT &&
	cut -f4 <list3.out >list3_pri.out &&
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

test_expect_success 'job-manager: cancel jobs' '
	for jobid in $(cut -f1 <list3.out); do \
		flux job cancel ${jobid}; \
	done
'

test_expect_success 'job-manager: queue contains 0 jobs' '
       test $(list_jobs | wc -l) -eq 0
'

test_expect_success 'job-manager: submit 10 jobs of equal priority' '
	rm -f submit10.out &&
	for i in $(seq 1 10); do \
	    flux job submit basic.json >>submit10.out; \
	done
'

test_expect_success 'job-manager: jobs are listed in submit order' '
	list_jobs >list10.out &&
	cut -f1 <list10.out >list10_ids.out &&
	test_cmp submit10.out list10_ids.out
'

test_expect_success 'job-manager: flux job priority sets last job priority=31' '
	lastid=$(tail -1 <list10_ids.out) &&
	flux job priority ${lastid} 31
'

test_expect_success 'job-manager: priority was updated in KVS' '
	jobid=$(tail -1 <list10_ids.out) &&
        flux job wait-event --timeout=5.0 ${jobid} priority &&
	flux job eventlog $jobid \
		| cut -d" " -f2- | grep ^priority >pri.out &&
	grep -q priority=31 pri.out
'

test_expect_success 'job-manager: that job is now the first job' '
	list_jobs >list10_reordered.out &&
	firstid=$(cut -f1 <list10_reordered.out | head -1) &&
	lastid=$(tail -1 <list10_ids.out) &&
	test "${lastid}" -eq "${firstid}"
'

test_expect_success 'job-manager: reload the job manager' '
	flux module remove job-manager &&
	flux module load job-manager
'

test_expect_success 'job-manager: queue was successfully reconstructed' '
	list_jobs >list_reload.out &&
	test_cmp list10_reordered.out list_reload.out
'

test_expect_success 'job-manager: cancel jobs' '
	for jobid in $(cut -f1 <list_reload.out); do \
		flux job cancel ${jobid}; \
	done &&
	test $(list_jobs | wc -l) -eq 0
'

test_expect_success 'job-manager: flux job priority fails on invalid priority' '
	jobid=$(flux job submit basic.json) &&
	flux job priority ${jobid} 31 &&
	test_must_fail flux job priority ${jobid} -1 &&
	test_must_fail flux job priority ${jobid} 32 &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: guest can reduce priority from default' '
	jobid=$(flux job submit  basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 5 &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: guest can increase to default' '
	jobid=$(flux job submit -p 0 basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 16 &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: guest cannot increase past default' '
	jobid=$(flux job submit basic.json) &&
	! FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 17 &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: guest can decrease from from >default' '
	jobid=$(flux job submit -p 31 basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job priority ${jobid} 17 &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: guest cannot set priority of others jobs' '
	jobid=$(flux job submit basic.json) &&
	newid=$(($(id -u)+1)) &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${newid} \
		flux job priority ${jobid} 0 &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: guest cannot cancel others jobs' '
	jobid=$(flux job submit basic.json) &&
	newid=$(($(id -u)+1)) &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${newid} \
		flux job cancel ${jobid} &&
	flux job cancel ${jobid}
'

test_expect_success 'job-manager: no jobs in the queue' '
	test $(list_jobs | wc -l) -eq 0
'

test_expect_success 'job-manager: reload the job manager' '
	flux module remove job-manager &&
	flux module load job-manager
'

test_expect_success 'job-manager: still no jobs in the queue' '
	test $(list_jobs | wc -l) -eq 0
'

test_expect_success 'job-manager: flux job drain works' '
	run_timeout 5 flux job drain
'

test_expect_success 'job-manager: flux job drain (3x) works' '
	flux job drain &&
	flux job drain &&
	flux job drain
'

test_expect_success 'job-manager: flux job submit receives disabled error' '
	! flux job submit basic.json 2>drain_submit.err &&
	grep disabled drain_submit.err
'

test_expect_success 'job-manager: flux job undrain works' '
	flux job undrain
'

test_expect_success 'job-manager: flux job submit works after undrain' '
	flux job submit basic.json
'

test_expect_success 'job-manager: flux job drain -t 0.01 receives timeout error' '
	! flux job drain -t 0.01 2>drain.err &&
	grep timeout drain.err
'

test_expect_success 'job-manager: flux job submit fails while drained' '
	! flux job submit basic.json
'

test_expect_success 'job-manager: flux job drain canceled by undrain' '
	${DRAIN_UNDRAIN} >drain_undrain.out &&
	grep re-enabled drain_undrain.out
'

test_expect_success 'job-manager: there is still one job in the queue' '
	list_jobs >list.out &&
	test $(wc -l <list.out) -eq 1
'

test_expect_success 'job-manager: drain unblocks when last job is canceled' '
	jobid=$(cut -f1 <list.out) &&
	run_timeout 5 ${DRAIN_CANCEL} ${jobid}
'

test_expect_success 'list request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.list 71 </dev/null
'
test_expect_success 'raise request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.raise 71 </dev/null
'
test_expect_success 'priority request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.priority 71 </dev/null
'
test_expect_success 'sched-ready request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.sched-ready 71 </dev/null
'
test_expect_success 'exec-hello request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.exec-hello 71 </dev/null
'
test_expect_success 'submit request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.submit 71 </dev/null
'

test_expect_success 'job-manager: remove job-manager, job-info, job-ingest' '
	flux module remove -r 0 job-manager &&
	flux module remove -r all job-info &&
	flux module remove -r all job-ingest
'

test_done
