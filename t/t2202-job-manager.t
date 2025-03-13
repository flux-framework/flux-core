#!/bin/sh

test_description='Test flux job manager service'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 kvs

flux setattr log-stderr-level 1

DRAIN_CANCEL="flux python ${FLUX_SOURCE_DIR}/t/job-manager/drain-cancel.py"
RPC=${FLUX_BUILD_DIR}/t/request/rpc
LIST_JOBS=${FLUX_BUILD_DIR}/t/job-manager/list-jobs
JOB_CONV="flux python ${FLUX_SOURCE_DIR}/t/job-manager/job-conv.py"

test_expect_success 'job-manager: generate jobspec for simple test job' '
	flux run --dry-run -n1 hostname >basic.json
'

test_expect_success 'job-manager: load job-ingest, job-info, job-manager' '
	flux module load job-manager &&
	flux module load job-ingest &&
	flux exec -r all -x 0 flux module load job-ingest &&
	flux module load job-info
'

test_expect_success 'job-manager: max_jobid=0 before jobs run' '
	test $(${RPC} job-manager.getinfo | jq .max_jobid) -eq 0
'

test_expect_success 'job-manager: submit one job' '
	flux job submit basic.json | flux job id >submit1.out
'

test_expect_success 'job-manager: max_jobid=last' '
	${RPC} job-manager.getinfo | jq .max_jobid >max1.out &&
	test_cmp submit1.out max1.out
'

test_expect_success 'job-manager: queue contains 1 job' '
	${LIST_JOBS} >list1.out &&
	test $(wc -l <list1.out) -eq 1
'

test_expect_success 'job-manager: queue lists job with correct jobid' '
	$jq .id <list1.out >list1_jobid.out &&
	test_cmp submit1.out list1_jobid.out
'

test_expect_success 'job-manager: queue lists job with state=N' '
	echo "SCHED" >list1_state.exp &&
	$jq .state <list1.out | ${JOB_CONV} statetostr >list1_state.out &&
	test_cmp list1_state.exp list1_state.out
'

test_expect_success 'job-manager: queue lists job with correct userid' '
	id -u >list1_userid.exp &&
	$jq .userid <list1.out >list1_userid.out &&
	test_cmp list1_userid.exp list1_userid.out
'

test_expect_success 'job-manager: queue list job with correct urgency' '
	echo 16 >list1_urgency.exp &&
	$jq .urgency <list1.out >list1_urgency.out &&
	test_cmp list1_urgency.exp list1_urgency.out
'

test_expect_success 'job-manager: queue list job with correct priority' '
	echo 16 >list1_priority.exp &&
	$jq .priority <list1.out >list1_priority.out &&
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

test_expect_success 'job-manager: exception note with embedded = is accepted' '
	jobid=$(cat list1_jobid.out) &&
	flux job raise --severity=1 --type=testing ${jobid} foo=bar
'

test_expect_success 'job-manager: queue contains 1 jobs' '
	test $(${LIST_JOBS} | wc -l) -eq 1
'

test_expect_success 'job-manager: cancel job' '
	jobid=$(cat list1_jobid.out) &&
	flux cancel ${jobid} &&
	flux job wait-event --timeout=5.0 --match-context=type=cancel ${jobid} exception &&
	flux job eventlog $jobid | grep exception \
		| grep severity=0 | grep "type=\"cancel\""
'

test_expect_success 'job-manager: queue contains 0 jobs' '
	test $(${LIST_JOBS} | wc -l) -eq 0
'

test_expect_success 'job-manager: submit jobs with urgency=min,default,max' '
	flux job submit -u0  basic.json | flux job id >submit_min.out &&
	flux job submit      basic.json | flux job id >submit_def.out &&
	flux job submit -u31 basic.json | flux job id >submit_max.out
'

test_expect_success 'job-manager: queue contains 3 jobs' '
	${LIST_JOBS} >list3.out &&
	test $(wc -l <list3.out) -eq 3
'

test_expect_success 'job-manager: queue is sorted in priority order' '
	cat >list3_priority.exp <<-EOT &&
	4294967295
	16
	0
	EOT
	$jq .priority <list3.out >list3_priority.out &&
	test_cmp list3_priority.exp list3_priority.out
'

test_expect_success 'job-manager: list-jobs --count shows highest priority jobs' '
	cat >list3_lim2.exp <<-EOT &&
	4294967295
	16
	EOT
	${LIST_JOBS} -c 2 | $jq .priority >list3_lim2.out &&
	test_cmp list3_lim2.exp list3_lim2.out
'

test_expect_success 'job-manager: priority listed as priority=4294967295 in KVS' '
	jobid=$(head -n 1 list3.out | $jq .id) &&
	flux job wait-event --timeout=5.0 ${jobid} priority &&
	flux job eventlog $jobid | grep priority=4294967295
'

test_expect_success 'job-manager: cancel jobs' '
	flux cancel $($jq .id <list3.out)
'

test_expect_success 'job-manager: queue contains 0 jobs' '
	test $(${LIST_JOBS} | wc -l) -eq 0
'

test_expect_success 'job-manager: submit 10 jobs of equal urgency' '
	rm -f submit10.out &&
	for i in $(seq 1 10); do \
	    flux job submit basic.json | flux job id >>submit10.out; \
	done
'

test_expect_success 'job-manager: jobs are listed in submit order' '
	${LIST_JOBS} >list10.out &&
	$jq .id <list10.out >list10_ids.out &&
	test_cmp submit10.out list10_ids.out
'

test_expect_success 'job-manager: flux job urgency sets last job urgency=31' '
	lastid=$(tail -1 <list10_ids.out) &&
	flux job urgency ${lastid} 31
'

test_expect_success 'job-manager: urgency was updated in KVS' '
	jobid=$(tail -1 <list10_ids.out) &&
	flux job wait-event --timeout=5.0 ${jobid} urgency &&
	flux job eventlog $jobid \
		| cut -d" " -f2- | grep ^urgency >urgency.out &&
	grep -q urgency=31 urgency.out
'

test_expect_success 'job-manager: priority was updated in KVS' '
	jobid=$(tail -1 <list10_ids.out) &&
	flux job wait-event --timeout=5.0 -c 2 ${jobid} priority &&
	flux job eventlog $jobid | grep ^priority | tail -n 1 | priority=4294967295
'

test_expect_success 'job-manager: that job is now the first job' '
	${LIST_JOBS} >list10_reordered.out &&
	firstid=$($jq .id <list10_reordered.out | head -1) &&
	lastid=$(tail -1 <list10_ids.out) &&
	test "${lastid}" -eq "${firstid}"
'

test_expect_success 'job-manager: jobs in state S w/ no scheduler' '
	for id in $(cat submit10.out); do \
		jmgr_check_state ${id} S; \
	done
'

test_expect_success 'job-manager: save max_jobid' '
	${RPC} job-manager.getinfo | jq .max_jobid >max2.exp
'

test_expect_success 'job-manager: reload the job manager' '
	flux module reload job-manager
'

test_expect_success 'job-manager: queue was successfully reconstructed' '
	${LIST_JOBS} >list_reload.out &&
	test_cmp list10_reordered.out list_reload.out
'

check_eventlog_restart_events() {
	for jobid in $($jq .id <list_reload.out); do
		if ! flux job wait-event -t 20 -c 1 ${jobid} flux-restart \
		   || ! flux job wait-event -t 20 -c 2 ${jobid} priority
		then
			return 1
		fi
	done
	return 0
}

test_expect_success 'job-manager: eventlog indicates restart & priority event' '
	check_eventlog_restart_events
'

test_expect_success 'job-manager: max_jobid has not changed' '
	${RPC} job-manager.getinfo | jq .max_jobid >max2.out &&
	test_cmp max2.exp max2.out
'

test_expect_success 'job-manager: cancel jobs' '
	flux cancel $($jq .id <list_reload.out) &&
	test $(${LIST_JOBS} | wc -l) -eq 0
'

test_expect_success 'job-manager: flux job urgency fails on invalid urgency' '
	jobid=$(flux job submit basic.json) &&
	flux job urgency ${jobid} 31 &&
	test_must_fail flux job urgency ${jobid} -1 &&
	test_must_fail flux job urgency ${jobid} 32 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: flux job urgency special args work' '
	jobid=$(flux job submit basic.json | flux job id) &&
	flux job urgency ${jobid} hold &&
	${LIST_JOBS} > list_hold.out &&
	test $(cat list_hold.out | grep ${jobid} | $jq .urgency) -eq 0 &&
	test $(cat list_hold.out | grep ${jobid} | $jq .priority) -eq 0 &&
	flux job urgency ${jobid} expedite &&
	${LIST_JOBS} > list_expedite.out &&
	test $(cat list_expedite.out | grep ${jobid} | $jq .urgency) -eq 31 &&
	test $(cat list_expedite.out | grep ${jobid} | $jq .priority) -eq 4294967295 &&
	flux job urgency ${jobid} default &&
	${LIST_JOBS} > list_default.out &&
	test $(cat list_default.out | grep ${jobid} | $jq .urgency) -eq 16 &&
	test $(cat list_default.out | grep ${jobid} | $jq .priority) -eq 16 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: flux job urgency -v work' '
	jobid=$(flux job submit basic.json | flux job id) &&
	flux job urgency -v ${jobid} 10 2>&1 | grep "16" &&
	flux job urgency -v ${jobid} hold 2>&1 | grep "10" &&
	flux job urgency -v ${jobid} expedite 2>&1 | grep "held" &&
	flux job urgency -v ${jobid} 10 2>&1 | grep "expedited" &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: guest can reduce urgency from default' '
	jobid=$(flux job submit  basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job urgency ${jobid} 5 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: guest can increase to default' '
	jobid=$(flux job submit -u 0 basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job urgency ${jobid} 16 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: guest cannot increase past default' '
	jobid=$(flux job submit basic.json) &&
	! FLUX_HANDLE_ROLEMASK=0x2 flux job urgency ${jobid} 17 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: guest can decrease from from >default' '
	jobid=$(flux job submit -u 31 basic.json) &&
	FLUX_HANDLE_ROLEMASK=0x2 flux job urgency ${jobid} 17 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: guest cannot set urgency of others jobs' '
	jobid=$(flux job submit basic.json) &&
	newid=$(($(id -u)+1)) &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${newid} \
		flux job urgency ${jobid} 0 &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: guest cannot cancel others jobs' '
	jobid=$(flux job submit basic.json) &&
	newid=$(($(id -u)+1)) &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${newid} \
		flux cancel ${jobid} &&
	flux cancel ${jobid}
'

test_expect_success 'job-manager: no jobs in the queue' '
	test $(${LIST_JOBS} | wc -l) -eq 0
'

# job-info module depends on job-manager, unload and reload it too
test_expect_success 'job-manager: reload the job manager' '
	flux module remove job-info &&
	flux module reload job-manager &&
	flux module load job-info
'

test_expect_success 'job-manager: still no jobs in the queue' '
	test $(${LIST_JOBS} | wc -l) -eq 0
'

test_expect_success 'job-manager: flux queue disable works' '
	flux queue disable --message="system is fubar"
'

test_expect_success 'job-manager: flux job submit receives custom error' '
	! flux job submit basic.json 2>disabled_submit.err &&
	grep fubar disabled_submit.err
'

test_expect_success 'job-manager: flux queue enable works' '
	flux queue enable
'

test_expect_success 'job-manager: flux job submit works after queue enable' '
	flux job submit basic.json
'

test_expect_success 'job-manager: flux queue drain -t 0.01 receives timeout error' '
	! flux queue drain -t 0.01 2>drain.err &&
	grep timeout drain.err
'

test_expect_success 'job-manager: there is still one job in the queue' '
	${LIST_JOBS} >list.out &&
	test $(wc -l <list.out) -eq 1
'

test_expect_success 'job-manager: drain unblocks when last job is canceled' '
	jobid=$($jq .id <list.out) &&
	run_timeout 5 ${DRAIN_CANCEL} ${jobid}
'

test_expect_success 'list request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.list 71 </dev/null
'
test_expect_success 'raise request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.raise 71 </dev/null
'
test_expect_success 'urgency request with empty payload fails with EPROTO(71)' '
	${RPC} job-manager.urgency 71 </dev/null
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

test_expect_success 'job-manager stats works' '
	flux module stats job-manager > stats.out &&
	cat stats.out | $jq -e .journal.listeners
'

test_expect_success 'flux module stats job-manager is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats job-manager >/dev/null
'

test_expect_success 'job-manager: remove job-info, job-manager, job-ingest' '
	flux module remove job-info &&
	flux module remove job-manager &&
	flux exec -r all flux module remove job-ingest
'

test_done
