#!/bin/sh

test_description='Test flux job list services'

. $(dirname $0)/job-list/job-list-helper.sh

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 job --test-hosts=node[0-3]

RPC=${FLUX_BUILD_DIR}/t/request/rpc
RPC_STREAM=${FLUX_BUILD_DIR}/t/request/rpc_stream
JOB_CONV="flux python ${FLUX_SOURCE_DIR}/t/job-manager/job-conv.py"
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py"

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

fj_wait_event() {
	flux job wait-event --timeout=20 "$@"
}

# To avoid raciness in tests, need to ensure that job state we care about
# testing against has been reached in the job-list module.
wait_jobid_state() {
	flux job list-ids --wait-state=$2 $1 > /dev/null
}

test_expect_success 'setup specific fake hostnames' '
	flux R encode -r 0-3 -c 0-1 -H node[0-3] \
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux module unload sched-simple &&
	flux module reload resource noverify &&
	flux module load sched-simple
'

test_expect_success 'create helper job submission script' '
	cat >sleepinf.sh <<-EOT &&
	#!/bin/sh
	echo "job started"
	sleep inf
	EOT
	chmod +x sleepinf.sh
'

# submit a whole bunch of jobs for job list testing
#
# - the first loop of job submissions are intended to have some jobs run
#   quickly and complete
# - the second loop of job submissions are intended to eat up all resources
# - the last job submissions are intended to get a create a set of
#   pending jobs, because jobs from the second loop have taken all resources
#   - we desire pending jobs sorted in priority order, so we need to
#   create the sorted list for comparison later.
# - job ids are stored in files in the order we expect them to be listed
#   - pending jobs - by priority (highest first), job id (smaller first)
#   - running jobs - by start time (most recent first)
#   - inactive jobs - by completion time (most recent first)
#
# TODO
# - alternate userid job listing

test_expect_success 'submit jobs for job list testing' '
	#
	# submit jobs that will complete
	#
	for i in $(seq 0 3); do
		flux submit --requires=host:node${i} hostname >> inactiveids
		fj_wait_event `tail -n 1 inactiveids` clean
	done &&
	#
	#  Currently all inactive ids are "completed"
	#
	tac inactiveids | flux job id > completed.ids &&
	#
	#  Run a job that will fail, copy its JOBID to both inactive and
	#	failed lists.
	#
	! jobid=`flux submit --requires=host:node0 --wait nosuchcommand` &&
	echo $jobid >> inactiveids &&
	flux job id $jobid > failedids &&
	#
	#  Run a job that we will end with a signal, copy its JOBID to both inactive and
	#	failed and terminated lists.
	#
	# N.B. sleepinf.sh and wait-event on job data to workaround
	# rare job startup race.  See #5210
	#
	jobid=`flux submit --requires=host:node0 ./sleepinf.sh` &&
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
	jobid=`flux submit --requires=host:node0 ./sleepinf.sh` &&
	flux job wait-event -W -p guest.output $jobid data &&
	flux job raise --type=myexception --severity=0 -m "myexception" $jobid &&
	fj_wait_event $jobid clean &&
	echo $jobid >> inactiveids &&
	flux job id $jobid > exception.ids &&
	flux job id $jobid >> failedids &&
	#
	#  Run a job that will timeout, copy its JOBID to both inactive and
	#	timeout lists.
	#
	jobid=`flux submit --requires=host:node0 --time-limit=0.5s sleep 30` &&
	echo $jobid >> inactiveids &&
	flux job id $jobid > timeout.ids &&
	fj_wait_event ${jobid} clean &&
	#
	#  Submit 8 sleep jobs to fill up resources
	#
	#  N.B. no need to specify --requires:host, will be distributed
	#  evenly
	#
	for i in $(seq 0 7); do
		flux submit --time-limit=5m sleep 600 >> runningids
	done &&
	tac runningids | flux job id > running.ids &&
	#
	#  Submit a set of jobs with misc urgencies
	#
	#  N.B. no need to specify --requires:host, these jobs wont run
	#
	id1=$(flux submit --urgency=20 hostname) &&
	id2=$(flux submit	       hostname) &&
	id3=$(flux submit --urgency=31 hostname) &&
	id4=$(flux submit --urgency=0  hostname) &&
	id5=$(flux submit --urgency=20 hostname) &&
	id6=$(flux submit	       hostname) &&
	id7=$(flux submit --urgency=31 hostname) &&
	id8=$(flux submit --urgency=0  hostname) &&
	flux job id $id3 > pending.ids &&
	flux job id $id7 >> pending.ids &&
	flux job id $id1 >> pending.ids &&
	flux job id $id5 >> pending.ids &&
	flux job id $id2 >> pending.ids &&
	flux job id $id6 >> pending.ids &&
	flux job id $id4 >> pending.ids &&
	flux job id $id8 >> pending.ids &&
	cat pending.ids > active.ids &&
	cat running.ids >> active.ids &&
	#
	#  Submit a job and cancel it
	#
	# N.B. no need to handle issue #5210 here, the job will not
	# run due to lack of resources.
	#
	jobid=`flux submit --job-name=canceledjob sleep 30` &&
	flux job wait-event $jobid depend &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean &&
	flux job id $jobid >> inactiveids &&
	flux job id $jobid > canceled.ids &&
	tac failedids | flux job id > failed.ids &&
	tac inactiveids | flux job id > inactive.ids &&
	cat active.ids > all.ids &&
	cat inactive.ids >> all.ids &&
	#
	#  The job-list module has eventual consistency with the jobs stored in
	#  the job-manager queue.  To ensure no raciness in tests, ensure
	#  jobs above have reached expected states in job-list before continuing.
	#
	flux job list-ids --wait-state=sched $(job_list_state_ids pending) > /dev/null &&
	flux job list-ids --wait-state=run $(job_list_state_ids running) > /dev/null &&
	flux job list-ids --wait-state=inactive $(job_list_state_ids inactive) > /dev/null
'

# Note: "running" = "run" | "cleanup", we also test just "run" state
# since we happen to know all these jobs are in the "run" state given
# checks above

test_expect_success 'flux job list running jobs in started order' '
	flux job list -s running | jq .id > list_started1.out &&
	flux job list -s run,cleanup | jq .id > list_started2.out &&
	flux job list -s run | jq .id > list_started3.out &&
	test_cmp list_started1.out running.ids &&
	test_cmp list_started2.out running.ids &&
	test_cmp list_started3.out running.ids
'

test_expect_success 'flux job list running jobs with correct state' '
	for count in `seq 1 8`; do \
		echo "RUN" >> list_state_R.exp; \
	done &&
	flux job list -s running | jq .state | ${JOB_CONV} statetostr > list_state_R1.out &&
	flux job list -s run,cleanup | jq .state | ${JOB_CONV} statetostr > list_state_R2.out &&
	flux job list -s run | jq .state | ${JOB_CONV} statetostr > list_state_R3.out &&
	test_cmp list_state_R1.out list_state_R.exp &&
	test_cmp list_state_R2.out list_state_R.exp &&
	test_cmp list_state_R3.out list_state_R.exp
'

test_expect_success 'flux job list no jobs in cleanup state' '
	count=$(flux job list -s cleanup | wc -l) &&
	test $count -eq 0
'

test_expect_success 'flux job list inactive jobs in completed order' '
	flux job list -s inactive | jq .id > list_inactive.out &&
	test_cmp list_inactive.out inactive.ids
'

test_expect_success 'flux job list inactive jobs with correct state' '
	for count in `seq 1 $(job_list_state_count inactive)`; do \
		echo "INACTIVE" >> list_state_I.exp; \
	done &&
	flux job list -s inactive | jq .state | ${JOB_CONV} statetostr > list_state_I.out &&
	test_cmp list_state_I.out list_state_I.exp
'

test_expect_success 'flux job list inactive jobs results are correct' '
	flux job list -s inactive | jq .result | ${JOB_CONV} resulttostr > list_result_I.out &&
	echo "CANCELED" >> list_result_I.exp &&
	echo "TIMEOUT" >> list_result_I.exp &&
	for count in `seq 1 3`; do \
		echo "FAILED" >> list_result_I.exp; \
	done &&
	for count in `seq 1 4`; do \
		echo "COMPLETED" >> list_result_I.exp; \
	done &&
	test_cmp list_result_I.out list_result_I.exp
'

# flux job list does not take results as an option, test via direct
# call to job-list.list

test_expect_success 'flux job list only canceled jobs' '
	id=$(id -u) &&
	result=`${JOB_CONV} strtoresult CANCELED` &&
	constraint="{ and: [ {userid:[${id}]}, {results:[${result}]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_result_canceled.out &&
	test_cmp canceled.ids list_result_canceled.out
'

test_expect_success 'flux job list only failed jobs' '
	id=$(id -u) &&
	result=`${JOB_CONV} strtoresult FAILED` &&
	constraint="{ and: [ {userid:[${id}]}, {results:[${result}]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_result_failed.out &&
	test_cmp failed.ids list_result_failed.out
'

test_expect_success 'flux job list only timeout jobs' '
	id=$(id -u) &&
	result=`${JOB_CONV} strtoresult TIMEOUT` &&
	constraint="{ and: [ {userid:[${id}]}, {results:[${result}]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_result_timeout.out &&
	test_cmp timeout.ids list_result_timeout.out
'

test_expect_success 'flux job list only completed jobs' '
	id=$(id -u) &&
	result=`${JOB_CONV} strtoresult COMPLETED` &&
	constraint="{ and: [ {userid:[${id}]}, {results:[${result}]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_result_completed.out &&
	test_cmp completed.ids list_result_completed.out
'

# Note: "pending" = "depend" | "sched", we also test just "sched"
# state since we happen to know all these jobs are in the "sched"
# state given checks above

test_expect_success 'flux job list pending jobs in priority order' '
	flux job list -s pending | jq .id > list_pending1.out &&
	flux job list -s depend,priority,sched | jq .id > list_pending2.out &&
	flux job list -s sched | jq .id > list_pending3.out &&
	test_cmp list_pending1.out pending.ids &&
	test_cmp list_pending2.out pending.ids &&
	test_cmp list_pending3.out pending.ids
'

test_expect_success 'flux job list pending jobs with correct urgency' '
	cat >list_urgency.exp <<-EOT &&
31
31
20
20
16
16
0
0
EOT
	flux job list -s pending | jq .urgency > list_urgency1.out &&
	flux job list -s depend,priority,sched | jq .urgency > list_urgency2.out &&
	flux job list -s sched | jq .urgency > list_urgency3.out &&
	test_cmp list_urgency1.out list_urgency.exp &&
	test_cmp list_urgency2.out list_urgency.exp &&
	test_cmp list_urgency3.out list_urgency.exp
'

test_expect_success 'flux job list pending jobs with correct priority' '
	cat >list_priority.exp <<-EOT &&
4294967295
4294967295
20
20
16
16
0
0
EOT
	flux job list -s pending | jq .priority > list_priority1.out &&
	flux job list -s depend,priority,sched | jq .priority > list_priority2.out &&
	flux job list -s sched | jq .priority > list_priority3.out &&
	test_cmp list_priority1.out list_priority.exp &&
	test_cmp list_priority2.out list_priority.exp &&
	test_cmp list_priority3.out list_priority.exp
'

test_expect_success 'flux job list pending jobs with correct state' '
	for count in `seq 1 8`; do \
		echo "SCHED" >> list_state_S.exp; \
	done &&
	flux job list -s sched | jq .state | ${JOB_CONV} statetostr > list_state_S.out &&
	test_cmp list_state_S.out list_state_S.exp
'

test_expect_success 'flux job list no jobs in depend state' '
	count=$(flux job list -s depend | wc -l) &&
	test $count -eq 0
'

# Note: "active" = "pending" | "running", i.e. depend, priority,
# sched, run, cleanup
test_expect_success 'flux job list active jobs in correct order' '
	flux job list -s active | jq .id > list_active1.out &&
	flux job list -s depend,priority,sched,run,cleanup | jq .id > list_active2.out &&
	flux job list -s sched,run | jq .id > list_active3.out &&
	test_cmp list_active1.out active.ids &&
	test_cmp list_active2.out active.ids &&
	test_cmp list_active3.out active.ids
'

test_expect_success 'flux job list jobs with correct userid' '
	for count in `seq 1 $(job_list_state_count all)`; do \
		id -u >> list_userid.exp; \
	done &&
	flux job list -a | jq .userid > list_userid.out &&
	test_cmp list_userid.out list_userid.exp
'

test_expect_success 'flux job list defaults to listing pending & running jobs' '
	flux job list | jq .id > list_default.out &&
	count=$(wc -l < list_default.out) &&
	test $count = $(job_list_state_count active) &&
	test_cmp list_default.out active.ids
'

test_expect_success 'flux job list --user=userid works' '
	uid=$(id -u) &&
	flux job list --user=$uid> list_userid.out &&
	count=$(wc -l < list_userid.out) &&
	test $count = $(job_list_state_count active)
'

test_expect_success 'flux job list --user=all works' '
	flux job list --user=all > list_all.out &&
	count=$(wc -l < list_all.out) &&
	test $count = $(job_list_state_count active)
'

# we hard count numbers here b/c its a --count test
test_expect_success 'flux job list --count works' '
	flux job list -s active,inactive --count=12 | jq .id > list_count.out &&
	count=$(wc -l < list_count.out) &&
	test "$count" = "12" &&
	cat pending.ids > list_count.exp &&
	head -n 4 running.ids >> list_count.exp &&
	test_cmp list_count.out list_count.exp
'

test_expect_success 'flux job list all jobs works' '
	flux job list -a | jq .id > list_all_jobids.out &&
	test_cmp all.ids list_all_jobids.out
'

test_expect_success 'flux module stats job-list is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats job-list >/dev/null
'

# do some more advanced constraint queries

test_expect_success 'flux job list hostname jobs' '
	id=$(id -u) &&
	constraint="{ and: [ {userid:[${id}]}, {name:[\"hostname\"]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_hostname_jobs.out &&
	numlines=$(cat pending.ids completed.ids | wc -l) &&
	test $(cat list_constraint_hostname_jobs.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list active hostname jobs' '
	id=$(id -u) &&
	constraint="{ and: [ {userid:[${id}]}, {states:[\"active\"]}, {name:[\"hostname\"]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_pending_hostname.out &&
	test_cmp list_constraint_pending_hostname.out pending.ids
'

test_expect_success 'flux job list inactive hostname jobs' '
	id=$(id -u) &&
	constraint="{ and: [ {userid:[${id}]}, {states:[\"inactive\"]}, {name:[\"hostname\"]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_inactive_hostname.out &&
	test_cmp list_constraint_inactive_hostname.out completed.ids
'

test_expect_success 'flux job list invalid queue' '
	id=$(id -u) &&
	constraint="{ and: [ {userid:[${id}]}, {queue:[\"blarg\"]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_invalid_queue.out &&
	test $(cat list_constraint_invalid_queue.out | wc -l) -eq 0
'

test_expect_success 'flux job list active (1)' '
	state1=`${JOB_CONV} strtostate SCHED` &&
	state2=`${JOB_CONV} strtostate RUN` &&
	constraint="{ or: [ {states:[${state1}]}, {states:[${state2}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_active1.out &&
	numlines=$(cat active.ids | wc -l) &&
	test $(cat list_constraint_active1.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list active (2)' '
	state1=`${JOB_CONV} strtostate INACTIVE` &&
	constraint="{ not: [ {states:[${state1}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_active2.out &&
	numlines=$(cat active.ids | wc -l) &&
	test $(cat list_constraint_active2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list pending jobs or inactive jobs (1)' '
	state1=`${JOB_CONV} strtostate SCHED` &&
	state2=`${JOB_CONV} strtostate INACTIVE` &&
	constraint="{ or: [ {states:[${state1}]}, {states:[${state2}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_pending_inactive1.out &&
	numlines=$(cat pending.ids inactive.ids | wc -l) &&
	test $(cat list_constraint_pending_inactive1.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list pending jobs or inactive jobs (2)' '
	state1=`${JOB_CONV} strtostate SCHED` &&
	state2=`${JOB_CONV} strtostate INACTIVE` &&
	constraint="{ or: [ {states:[${state1}, ${state2}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_pending_inactive2.out &&
	numlines=$(cat pending.ids inactive.ids | wc -l) &&
	test $(cat list_constraint_pending_inactive2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list failed and canceled jobs (1)' '
	result1=`${JOB_CONV} strtoresult FAILED` &&
	result2=`${JOB_CONV} strtoresult CANCELED` &&
	constraint="{ or: [ {results:[${result1}]}, {results:[${result2}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_failed_canceled1.out &&
	numlines=$(cat canceled.ids failed.ids | wc -l) &&
	test $(cat list_constraint_failed_canceled1.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list failed and canceled jobs (2)' '
	result1=`${JOB_CONV} strtoresult FAILED` &&
	result2=`${JOB_CONV} strtoresult CANCELED` &&
	constraint="{ and: [ {userid:[${id}]}, {results:[${result1}, ${result2}]}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_failed_canceled2.out &&
	numlines=$(cat canceled.ids failed.ids | wc -l) &&
	test $(cat list_constraint_failed_canceled2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list pending jobs or failed jobs (1)' '
	state1=`${JOB_CONV} strtostate SCHED` &&
	state2=`${JOB_CONV} strtostate INACTIVE` &&
	result1=`${JOB_CONV} strtoresult FAILED` &&
	constraint="{ or: [ {states:[${state1}]}, {and: [ {states:[${state2}]}, {results:[${result1}]} ] } ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_pending_failed1.out &&
	numlines=$(cat pending.ids failed.ids | wc -l) &&
	test $(cat list_constraint_pending_failed1.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list pending jobs or failed jobs (2)' '
	state1=`${JOB_CONV} strtostate SCHED` &&
	result1=`${JOB_CONV} strtoresult FAILED` &&
	constraint="{ or: [ {states:[${state1}]}, {results:[${result1}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_pending_failed2.out &&
	numlines=$(cat pending.ids failed.ids | wc -l) &&
	test $(cat list_constraint_pending_failed2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list inactive (1)' '
	state1=`${JOB_CONV} strtostate INACTIVE` &&
	constraint="{ or: [ {states:[${state1}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_inactive1.out &&
	numlines=$(cat inactive.ids | wc -l) &&
	test $(cat list_constraint_inactive1.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list inactive (2)' '
	state1=`${JOB_CONV} strtostate SCHED` &&
	state2=`${JOB_CONV} strtostate RUN` &&
	constraint="{ not: [ { or: [ {states:[${state1}]}, {states:[${state2}]} ] } ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_inactive2.out &&
	numlines=$(cat inactive.ids | wc -l) &&
	test $(cat list_constraint_inactive2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list have run via t_run (1)' '
	constraint="{ or: [ {t_run:[\">=0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_t_run1.out &&
	numlines=$(cat running.ids failed.ids timeout.ids completed.ids | wc -l) &&
	test $(cat list_constraint_t_run1.out | wc -l) -eq ${numlines}
'

# use a floating point in this one
test_expect_success 'flux job list have run via t_run (2)' '
	constraint="{ or: [ {t_run:[\">=1.0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_t_run2.out &&
	numlines=$(cat running.ids failed.ids timeout.ids completed.ids | wc -l) &&
	test $(cat list_constraint_t_run2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list have run via t_run (3)' '
	constraint="{ or: [ {t_run:[\">1.1\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_t_run3.out &&
	numlines=$(cat running.ids failed.ids timeout.ids completed.ids | wc -l) &&
	test $(cat list_constraint_t_run3.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list inactive via t_inactive (1)' '
	constraint="{ or: [ {t_inactive:[\">=0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_t_inactive1.out &&
	numlines=$(cat inactive.ids | wc -l) &&
	test $(cat list_constraint_t_inactive1.out | wc -l) -eq ${numlines}
'

# use a floating point in this one
test_expect_success 'flux job list inactive via t_inactive (2)' '
	constraint="{ or: [ {t_inactive:[\">=1.0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_t_inactive2.out &&
	numlines=$(cat inactive.ids | wc -l) &&
	test $(cat list_constraint_t_inactive2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list inactive via t_inactive (3)' '
	constraint="{ or: [ {t_inactive:[\">1.1\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_t_inactive3.out &&
	numlines=$(cat inactive.ids | wc -l) &&
	test $(cat list_constraint_t_inactive3.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list none via t_inactive (1)' '
	constraint="{ or: [ {t_inactive:[\"<0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_none1.out &&
	test $(cat list_constraint_none1.out | wc -l) -eq 0
'

test_expect_success 'flux job list none via t_inactive (2)' '
	constraint="{ or: [ {t_inactive:[\"<=0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_none1.out &&
	test $(cat list_constraint_none1.out | wc -l) -eq 0
'

test_expect_success 'flux job list all via t_depend (1)' '
	constraint="{ or: [ {t_depend:[\">=0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_all1.out &&
	numlines=$(cat all.ids | wc -l) &&
	test $(cat list_constraint_all1.out | wc -l) -eq ${numlines}
'

# use a floating point in this one
test_expect_success 'flux job list all via t_depend (2)' '
	constraint="{ or: [ {t_depend:[\">=1.0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_all2.out &&
	numlines=$(cat all.ids | wc -l) &&
	test $(cat list_constraint_all2.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list all via t_depend (3)' '
	constraint="{ or: [ {t_depend:[\">1.1\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > list_constraint_all3.out &&
	numlines=$(cat all.ids | wc -l) &&
	test $(cat list_constraint_all3.out | wc -l) -eq ${numlines}
'

#
# nodelist / hostlist constraint filtering
#

# N.B. all failed and timeout jobs we explicitly ran on node0, so
# tests below don't test against node0 to make things easier.

# N.B. failed.ids are jobs that failed after running, thus will have
# had a node assigned.  timeout.ids obviously ran on a node, but timed
# out.
test_expect_success 'flux job list all jobs that ran on any node (1)' '
	constraint="{ and: [ {hostlist:[\"node[0-3]\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist1.out &&
	numlines=$(cat completed.ids running.ids failed.ids timeout.ids | wc -l) &&
	test $(cat constraint_hostlist1.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list all jobs that ran on any node (2)' '
	constraint="{ and: [ {hostlist:[\"node[2-3]\", \"node1\", \"node0\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist2.out &&
	numlines=$(cat completed.ids running.ids failed.ids timeout.ids | wc -l) &&
	test $(cat constraint_hostlist2.out | wc -l) -eq ${numlines}
'

# We evenly distributed non-bad jobs on nodes, so should be half of the jobs
test_expect_success 'flux job list all jobs that ran on nodes[1-2] (1)' '
	constraint="{ and: [ {hostlist:[\"node[1-2]\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist3.out &&
	numlines=$(expr $(cat completed.ids running.ids | wc -l) / 2) &&
	test $(cat constraint_hostlist3.out | wc -l) -eq ${numlines}
'

# We evenly distributed non-bad jobs on nodes, so should be half of the jobs
test_expect_success 'flux job list all jobs that ran on nodes[1-2] (2)' '
	constraint="{ and: [ {hostlist:[\"node1\", \"node2\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist4.out &&
	numlines=$(expr $(cat completed.ids running.ids | wc -l) / 2) &&
	test $(cat constraint_hostlist4.out | wc -l) -eq ${numlines}
'

# We evenly distributed non-bad jobs on nodes, so should be quarter of the jobs
test_expect_success 'flux job list all jobs that ran on node3' '
	constraint="{ and: [ {hostlist:[\"node3\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist5.out &&
	numlines=$(expr $(cat completed.ids running.ids | wc -l) / 4) &&
	test $(cat constraint_hostlist5.out | wc -l) -eq ${numlines}
'

# We evenly distributed completed jobs on nodes, so should be quarter of the jobs
test_expect_success 'flux job list completed jobs that ran on node3' '
	state=`${JOB_CONV} strtostate INACTIVE` &&
	constraint="{ and: [ {hostlist:[\"node3\"]}, {states:[${state}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist6.out &&
	numlines=$(expr $(cat completed.ids | wc -l) / 4) &&
	test $(cat constraint_hostlist6.out | wc -l) -eq ${numlines}
'

# We evenly distributed running jobs on nodes, so should be quarter of the jobs
test_expect_success 'flux job list running jobs that ran on node3' '
	state=`${JOB_CONV} strtostate RUNNING` &&
	constraint="{ and: [ {hostlist:[\"node3\"]}, {states:[${state}]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist7.out &&
	numlines=$(expr $(cat running.ids | wc -l) / 4) &&
	test $(cat constraint_hostlist7.out | wc -l) -eq ${numlines}
'

# We evenly distributed running jobs on nodes, so should be half of the jobs
# For this test, get start time of first running job
test_expect_success 'flux job list of running jobs that ran on node[1-2] after certain time (1)' '
	id=`tail -n1 running.ids` &&
	t_submit=`flux job list-ids ${id} | jq .t_submit` &&
	constraint="{ and: [ {hostlist:[\"node[1-2]\"]}, {t_submit:[\">=${t_submit}\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist8.out &&
	numlines=$(expr $(cat running.ids | wc -l) / 2) &&
	test $(cat constraint_hostlist8.out | wc -l) -eq ${numlines}
'

# We evenly distributed running jobs on nodes, so should be half of the jobs
# For this test, get last inactive time of completed jobs
test_expect_success 'flux job list of running jobs that ran on node[1-2] after certain time (2)' '
	id=`head -n1 completed.ids` &&
	t_inactive=`flux job list-ids ${id} | jq .t_inactive` &&
	constraint="{ and: [ {hostlist:[\"node[1-2]\"]}, {t_submit:[\">${t_inactive}\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist9.out &&
	numlines=$(expr $(cat running.ids | wc -l) / 2) &&
	test $(cat constraint_hostlist9.out | wc -l) -eq ${numlines}
'

# We evenly distributed completed & running jobs on nodes, so should be half of the jobs
test_expect_success 'flux job list of all jobs that ran on node[1-2] after certain time' '
	id=`head -n1 completed.ids` &&
	constraint="{ and: [ {hostlist:[\"node[1-2]\"]}, {t_submit:[\">5\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist9.out &&
	numlines=$(expr $(cat completed.ids running.ids | wc -l) / 2) &&
	test $(cat constraint_hostlist9.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list by rank (0-3)' '
	constraint="{ and: [ {ranks:[\"0-3\"]} ] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > constraint_hostlist1.out &&
	numlines=$(cat completed.ids running.ids failed.ids timeout.ids | wc -l) &&
	test $(cat constraint_hostlist1.out | wc -l) -eq ${numlines}
'
test_expect_success 'flux job list by rank (3)' '
	constraint="{ and: [ {ranks:[\"3\"]} ] }" &&
	jq -j -c -n  \
	  "{max_entries:1000, attrs:[\"ranks\"], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list | jq -r .jobs[].ranks | uniq > jobs.ranks &&
	test $(cat jobs.ranks) -eq 3
'
test_expect_success 'flux job list by rank (does not exist)' '
	constraint="{ and: [ {ranks:[\"10\"]} ] }" &&
	jq -j -c -n  \
	  "{max_entries:1000, attrs:[\"ranks\"], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list > jobs.noranks &&
	test_cmp /dev/null jobs.noranks
'
test_expect_success 'flux job list by rank (invalid)' '
	constraint="{ and: [ {ranks:[3]} ] }" &&
	jq -j -c -n  \
	  "{max_entries:1000, attrs:[\"ranks\"], constraint:${constraint}}" \
	  | $RPC_STREAM job-list.list 71 "value must be a string"
'
#
# legacy RPC tests
#

test_expect_success 'flux job list all hostname jobs (legacy RPC)' '
	id=$(id -u) &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:0, results:0, name:\"hostname\", attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_hostname_jobs.out &&
	numlines=$(cat pending.ids completed.ids | wc -l) &&
	test $(cat legacy_hostname_jobs.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list active hostname jobs (legacy RPC)' '
	id=$(id -u) &&
	state=`${JOB_CONV} strtostate ACTIVE` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:0, name:\"hostname\", attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_pending_hostname.out &&
	test_cmp legacy_pending_hostname.out pending.ids
'

test_expect_success 'flux job list inactive hostname jobs (legacy RPC)' '
	id=$(id -u) &&
	state=`${JOB_CONV} strtostate INACTIVE` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:0, name:\"hostname\", attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_inactive_hostname.out &&
	test_cmp legacy_inactive_hostname.out completed.ids
'

test_expect_success 'flux job list invalid queue (legacy RPC)' '
	id=$(id -u) &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:0, results:0, queue:\"foof\", attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_invalid_queue.out &&
	test $(cat legacy_invalid_queue.out | wc -l) -eq 0
'

test_expect_success 'flux job list active (legacy RPC)' '
	state=`${JOB_CONV} strtostate ACTIVE` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:0, attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_active.out &&
	numlines=$(cat active.ids | wc -l) &&
	test $(cat legacy_active.out | wc -l) -eq ${numlines}
'

test_expect_success 'flux job list only canceled jobs (legacy RPC)' '
	id=$(id -u) &&
	state=`${JOB_CONV} strtostate INACTIVE` &&
	result=`${JOB_CONV} strtoresult CANCELED` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:${result}, attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_result_canceled.out &&
	test_cmp canceled.ids legacy_result_canceled.out
'

test_expect_success 'flux job list only failed jobs (legacy RPC)' '
	id=$(id -u) &&
	state=`${JOB_CONV} strtostate INACTIVE` &&
	result=`${JOB_CONV} strtoresult FAILED` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:${result}, attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_result_failed.out &&
	test_cmp failed.ids legacy_result_failed.out
'

test_expect_success 'flux job list only timeout jobs (legacy RPC)' '
	id=$(id -u) &&
	state=`${JOB_CONV} strtostate INACTIVE` &&
	result=`${JOB_CONV} strtoresult TIMEOUT` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:${result}, attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_result_timeout.out &&
	test_cmp timeout.ids legacy_result_timeout.out
'

test_expect_success 'flux job list only completed jobs (legacy RPC)' '
	id=$(id -u) &&
	state=`${JOB_CONV} strtostate INACTIVE` &&
	result=`${JOB_CONV} strtoresult COMPLETED` &&
	jq -j -c -n  "{max_entries:1000, userid:${id}, states:${state}, results:${result}, attrs:[]}" \
	  | $RPC_STREAM job-list.list | jq .jobs[].id > legacy_result_completed.out &&
	test_cmp completed.ids legacy_result_completed.out
'

# non-streaming backwards compatibility

test_expect_success 'flux job list works with non-streaming RPC' '
	id=$(id -u) &&
	constraint="{ userid:[${id}] }" &&
	jq -j -c -n  "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | $RPC job-list.list | jq .jobs[].id > list_constraint_backwards.out &&
	numlines=$(cat all.ids | wc -l) &&
	test $(cat list_constraint_backwards.out | wc -l) -eq ${numlines}
'

# with single anonymous queue, queues arrays should be zero length
test_expect_success 'job stats lists jobs in correct state (mix)' '
	flux job stats | jq -e ".job_states.depend == 0" &&
	flux job stats | jq -e ".job_states.priority == 0" &&
	flux job stats | jq -e ".job_states.sched == $(job_list_state_count pending)" &&
	flux job stats | jq -e ".job_states.run == $(job_list_state_count running)" &&
	flux job stats | jq -e ".job_states.cleanup == 0" &&
	flux job stats | jq -e ".job_states.inactive == $(job_list_state_count inactive)" &&
	flux job stats | jq -e ".job_states.total == $(job_list_state_count all)" &&
	flux job stats | jq -e ".successful == $(job_list_state_count completed)" &&
	flux job stats | jq -e ".failed == $(job_list_state_count failed)" &&
	flux job stats | jq -e ".canceled == $(job_list_state_count canceled)" &&
	flux job stats | jq -e ".timeout == $(job_list_state_count timeout)" &&
	flux job stats | jq -e ".inactive_purged == 0" &&
	queuelength=$(flux job stats | jq ".queues | length") &&
	test ${queuelength} -eq 0
'

# Note: these wait-state tests must be before all jobs are canceled below
test_expect_success 'flux job list-ids works with --wait-state' '
	id=`head -n 1 pending.ids` &&
	flux job list-ids --wait-state=sched $id | jq -e ".state == 8"  &&
	id=`head -n 1 running.ids` &&
	flux job list-ids --wait-state=sched $id > /dev/null &&
	flux job list-ids --wait-state=run $id | jq -e ".state == 16" &&
	id=`head -n 1 completed.ids` &&
	flux job list-ids --wait-state=sched $id > /dev/null &&
	flux job list-ids --wait-state=run $id > /dev/null &&
	flux job list-ids --wait-state=inactive $id | jq -e ".state == 64" &&
	id=`head -n 1 canceled.ids` &&
	flux job list-ids --wait-state=sched $id > /dev/null &&
	flux job list-ids --wait-state=run $id > /dev/null &&
	flux job list-ids --wait-state=inactive $id | jq -e ".state == 64"
'

test_expect_success 'flux job list-ids fail with bad --wait-state' '
	id=`head -n 1 pending.ids` &&
	test_must_fail flux job list-ids --wait-state=foo $id > /dev/null
'


test_expect_success 'cleanup job listing jobs ' '
	# NOTE: do not use flux cancel `cat active.ids` as it races
	# with the reconstruction of job-list somehow
	for jobid in `cat active.ids`; do
	flux cancel $jobid &&
		fj_wait_event $jobid clean
	done
'

wait_inactive() {
	local i=0
	while [ "$(flux job list --states=inactive | wc -l)" != "$(job_list_state_count all)" ] \
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

# Note: annotations are not part of the following comparison
# because they are omitted from the in-memory journal history
test_expect_success 'reload the job-list module' "
	flux job list -a | jq 'del(.annotations)' > before_reload.out &&
	flux module reload job-list &&
	wait_inactive
"

test_expect_success 'job-list: list successfully reconstructed' "
	flux job list -a | jq 'del(.annotations)' > after_reload.out &&
	test_cmp before_reload.out after_reload.out
"

# the canceled checks may look confusing.  We canceled all active jobs
# right above here, so all those active jobs became canceled as a result
test_expect_success 'job stats lists jobs in correct state (all inactive)' '
	flux job stats | jq -e ".job_states.depend == 0" &&
	flux job stats | jq -e ".job_states.priority == 0" &&
	flux job stats | jq -e ".job_states.sched == 0" &&
	flux job stats | jq -e ".job_states.run == 0" &&
	flux job stats | jq -e ".job_states.cleanup == 0" &&
	flux job stats | jq -e ".job_states.inactive == $(job_list_state_count all)" &&
	flux job stats | jq -e ".job_states.total == $(job_list_state_count all)" &&
	flux job stats | jq -e ".successful == $(job_list_state_count completed)" &&
	flux job stats | jq -e ".failed == $(job_list_state_count failed)" &&
	flux job stats | jq -e ".canceled == $(job_list_state_count active canceled)" &&
	flux job stats | jq -e ".timeout == $(job_list_state_count timeout)" &&
	flux job stats | jq -e ".inactive_purged == 0" &&
	queuelength=$(flux job stats | jq ".queues | length") &&
	test ${queuelength} -eq 0
'

# job list-inactive

test_expect_success 'flux job list-inactive lists all inactive jobs' '
	flux job list-inactive > list-inactive.out &&
	count=`cat list-inactive.out | wc -l` &&
	test $count -eq $(job_list_state_count all)
'

test_expect_success 'flux job list-inactive w/ since 0 lists all inactive jobs' '
	count=`flux job list-inactive --since=0 | wc -l` &&
	test $count -eq $(job_list_state_count all)
'

# we hard count numbers here b/c its a --count test
test_expect_success 'flux job list-inactive w/ count limits output of inactive jobs' '
	count=`flux job list-inactive --count=14 | wc -l` &&
	test $count -eq 14
'

test_expect_success 'flux job list-inactive w/ since -1 leads to error' '
	test_must_fail flux job list-inactive --since=-1 > list_inactive_error1.out 2>&1 &&
	grep "invalid payload: since" list_inactive_error1.out
'

test_expect_success 'flux job list-inactive w/ count -1 leads to error' '
	test_must_fail flux job list-inactive --count=-1 > list_inactive_error2.out 2>&1 &&
	grep "invalid payload: max_entries" list_inactive_error2.out
'

test_expect_success 'flux job list-inactive w/ since (most recent timestamp)' '
	timestamp=`cat list-inactive.out | head -n 1 | jq .t_inactive` &&
	count=`flux job list-inactive --since=${timestamp} | wc -l` &&
	test $count -eq 0
'

test_expect_success 'flux job list-inactive w/ since (second to most recent timestamp)' '
	timestamp=`cat list-inactive.out | head -n 2 | tail -n 1 | jq .t_inactive` &&
	count=`flux job list-inactive --since=${timestamp} | wc -l` &&
	test $count -eq 1
'

test_expect_success 'flux job list-inactive w/ since (oldest timestamp)' '
	timestamp=`cat list-inactive.out | tail -n 1 | jq .t_inactive` &&
	count=`flux job list-inactive --since=${timestamp} | wc -l` &&
	test $count -eq 24
'

test_expect_success 'flux job list-inactive w/ since (middle timestamp #1)' '
	timestamp=`cat list-inactive.out | head -n 8 | tail -n 1 | jq .t_inactive` &&
	count=`flux job list-inactive --since=${timestamp} | wc -l` &&
	test $count -eq 7
'

test_expect_success 'flux job list-inactive w/ since (middle timestamp #2)' '
	timestamp=`cat list-inactive.out | head -n 13 | tail -n 1 | jq .t_inactive` &&
	count=`flux job list-inactive --since=${timestamp} | wc -l` &&
	test $count -eq 12
'


# job list-id

test_expect_success 'flux job list-ids works with a single ID' '
	id=`head -n 1 pending.ids` &&
	flux job list-ids $id | jq -e ".id == ${id}" &&
	id=`head -n 1 running.ids` &&
	flux job list-ids $id | jq -e ".id == ${id}" &&
	id=`head -n 1 inactive.ids` &&
	flux job list-ids $id | jq -e ".id == ${id}"
'

test_expect_success 'flux job list-ids multiple IDs works' '
	ids=$(job_list_state_ids pending) &&
	flux job list-ids $ids | jq .id > list_idsP.out &&
	test_cmp list_idsP.out pending.ids &&
	ids=$(job_list_state_ids running) &&
	flux job list-ids $ids | jq .id > list_idsR.out &&
	test_cmp list_idsR.out running.ids &&
	ids=$(job_list_state_ids inactive) &&
	flux job list-ids $ids | jq .id > list_idsI.out &&
	test_cmp list_idsI.out inactive.ids &&
	ids=$(job_list_state_ids all) &&
	flux job list-ids $ids | jq .id > list_idsPRI.out &&
	cat pending.ids running.ids inactive.ids > list_idsPRI.exp &&
	test_cmp list_idsPRI.exp list_idsPRI.out
'

test_expect_success 'flux job list-ids fails without ID' '
	test_must_fail flux job list-ids > list_ids_error1.out 2>&1 &&
	grep "Usage" list_ids_error1.out
'

test_expect_success 'flux job list-ids fails with bad ID' '
	test_must_fail flux job list-ids 1234567890 > list_ids_error2.out 2>&1 &&
	grep "No such file or directory" list_ids_error2.out
'

test_expect_success 'flux job list-ids fails with not an ID' '
	test_must_fail flux job list-ids foobar > list_ids_error3.out 2>&1 &&
	grep "No such file or directory" list_ids_error3.out
'

test_expect_success 'flux job list-ids fails with one bad ID out of several' '
	id1=`head -n 1 pending.ids` &&
	id2=`head -n 1 running.ids` &&
	id3=`head -n 1 inactive.ids` &&
	test_must_fail flux job list-ids ${id1} ${id2} 1234567890 ${id3} \
		> list_ids_error4.out 2>&1 &&
	grep "No such file or directory" list_ids_error4.out
'

# In order to test potential racy behavior, use job state pause/unpause to pause
# the handling of job state transitions from the job-manager.
#
# Note that between the background process of `flux job list-ids` and
# `unpause`, we must ensure the background process has sent the
# request to the job-list service and is now waiting for the id to be
# synced.  We call wait_idsync to check stats for this to ensure the
# racy behavior is covered.

wait_idsync() {
	local num=$1
	local i=0
	while (! flux module stats --parse idsync.waits job-list > /dev/null 2>&1 \
		   || [ "$(flux module stats --parse idsync.waits job-list 2> /dev/null)" != "$num" ]) \
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

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job ids (one id)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait hostname | flux job id`
	flux job list-ids ${jobid} > list_id_wait1.out &
	pid=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	cat list_id_wait1.out | jq -e ".id == ${jobid}"
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job ids (different ids)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid1=`flux submit --wait hostname | flux job id`
	jobid2=`flux submit --wait hostname | flux job id`
	flux job list-ids ${jobid1} ${jobid2} > list_id_wait2.out &
	pid=$!
	wait_idsync 2 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	grep ${jobid1} list_id_wait2.out &&
	grep ${jobid2} list_id_wait2.out
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job ids (same id)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait hostname | flux job id`
	flux job list-ids ${jobid} > list_id_wait3A.out &
	pid1=$!
	flux job list-ids ${jobid} > list_id_wait3B.out &
	pid2=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait ${pid1} &&
	wait ${pid2} &&
	cat list_id_wait3A.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait3B.out | jq -e ".id == ${jobid}"
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job id state (depend)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait-event=start sleep inf | flux job id`
	flux job list-ids --wait-state=depend ${jobid} > list_id_wait_state_depend.out &
	pid=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	cat list_id_wait_state_depend.out | jq -e ".id == ${jobid}"
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job id state (run)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait-event=start sleep inf | flux job id`
	flux job list-ids --wait-state=run ${jobid} > list_id_wait_state_run.out &
	pid=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	cat list_id_wait_state_run.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait_state_run.out | jq .state | ${JOB_CONV} statetostr > list_id_state_run.out &&
	grep RUN list_id_state_run.out
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job id state (cleanup)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait-event=start sleep inf | flux job id`
	flux job list-ids --wait-state=cleanup ${jobid} > list_id_wait_state_cleanup.out &
	pid=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	wait $pid &&
	cat list_id_wait_state_cleanup.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait_state_cleanup.out | jq .state | ${JOB_CONV} statetostr > list_id_state_cleanup.out &&
	grep CLEANUP list_id_state_cleanup.out
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job id state (inactive)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait-event=start sleep inf | flux job id`
	flux job list-ids --wait-state=inactive ${jobid} > list_id_wait_state_inactive.out &
	pid=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	wait $pid &&
	cat list_id_wait_state_inactive.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait_state_inactive.out | jq .state | ${JOB_CONV} statetostr > list_id_state_inactive.out &&
	grep INACTIVE list_id_state_inactive.out
'

# The job should never reach job state b/c we cancel it before it can
# run, so will return when job becomes inactive
test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job id state (run - cancel)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	flux queue stop &&
	jobid=`flux submit sleep inf | flux job id`
	flux job list-ids --wait-state=run ${jobid} > list_id_wait_state_run_cancel.out &
	pid=$!
	wait_idsync 1 &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	flux queue start &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	cat list_id_wait_state_run_cancel.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait_state_run_cancel.out | jq .state | ${JOB_CONV} statetostr > list_id_state_run_cancel.out &&
	grep INACTIVE list_id_state_run_cancel.out
'

# Can't guarantee output order, so grep for jobid instead of match jobid
test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job ids state (different ids)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid1=`flux submit --wait-event=start sleep inf | flux job id`
	jobid2=`flux submit --wait-event=start sleep inf | flux job id`
	flux job list-ids --wait-state=run ${jobid1} ${jobid2} > list_id_wait_state_different_ids.out &
	pid=$!
	wait_idsync 2 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	flux cancel $jobid1 $jobid2 &&
	fj_wait_event $jobid1 clean >/dev/null &&
	fj_wait_event $jobid2 clean >/dev/null &&
	grep ${jobid1} list_id_wait_state_different_ids.out &&
	grep ${jobid2} list_id_wait_state_different_ids.out &&
	head -n1 list_id_wait_state_different_ids.out | jq .state | ${JOB_CONV} statetostr > list_id_state_different_idsA.out &&
	tail -n1 list_id_wait_state_different_ids.out | jq .state | ${JOB_CONV} statetostr > list_id_state_different_idsB.out &&
	grep RUN list_id_state_different_idsA.out &&
	grep RUN list_id_state_different_idsB.out
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids waits for job ids state (same id)' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux submit --wait-event=start sleep inf | flux job id`
	flux job list-ids --wait-state=run ${jobid} > list_id_wait_state_same_idsA.out &
	pid1=$!
	flux job list-ids --wait-state=cleanup ${jobid} > list_id_wait_state_same_idsB.out &
	pid2=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait ${pid1} &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	wait ${pid2} &&
	cat list_id_wait_state_same_idsA.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait_state_same_idsB.out | jq -e ".id == ${jobid}" &&
	cat list_id_wait_state_same_idsA.out | jq .state | ${JOB_CONV} statetostr > list_id_state_same_idsA.out &&
	grep RUN list_id_state_same_idsA.out &&
	cat list_id_wait_state_same_idsB.out | jq .state | ${JOB_CONV} statetostr > list_id_state_same_idsB.out &&
	grep CLEANUP list_id_state_same_idsB.out
'

#
# job list timing
#

# simply test that value in timestamp increases through job states
test_expect_success 'flux job list job state timing outputs valid (job inactive)' '
	jobid=$(flux submit --wait hostname | flux job id) &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".t_submit < .t_depend" &&
	echo $obj | jq -e ".t_depend < .t_run" &&
	echo $obj | jq -e ".t_run < .t_cleanup" &&
	echo $obj | jq -e ".t_cleanup < .t_inactive"
'

# since job is running, make sure latter states don't exist
test_expect_success 'flux job list job state timing outputs valid (job running)' '
	jobid=$(flux submit sleep 60 | flux job id) &&
	fj_wait_event $jobid start >/dev/null &&
	wait_jobid_state $jobid run &&
	obj=$(flux job list -s running | grep $jobid) &&
	echo $obj | jq -e ".t_submit < .t_depend" &&
	echo $obj | jq -e ".t_depend < .t_run" &&
	echo $obj | jq -e ".t_cleanup == null" &&
	echo $obj | jq -e ".t_inactive == null" &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null
'

# `flux module reload job-list` does not ensure `job-list` is actually
# ready to service requests immediately afterwards (i.e. has list
# service been setup yet).  This checks to make sure `job-list` is
# really up and ready.
wait_job_list_ready() {
	local i=0
	while ( ! flux job list > /dev/null && [ $i -lt 50 ] )
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

#
# job names
#

test_expect_success 'flux job list outputs user job name' '
	jobid=`flux submit --wait --setattr system.job.name=foobar A B C | flux job id` &&
	echo $jobid > jobname1.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".name == \"foobar\""
'

test_expect_success 'flux job lists first argument for job name' '
	jobid=`flux submit --wait mycmd arg1 arg2 | flux job id` &&
	echo $jobid > jobname2.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".name == \"mycmd\""
'

test_expect_success 'flux job lists basename of first argument for job name' '
	jobid=`flux submit --wait /foo/bar arg1 arg2 | flux job id` &&
	echo $jobid > jobname3.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".name == \"bar\""
'

test_expect_success 'flux job lists full path for job name if basename fails on first arg' '
	jobid=`flux submit --wait /foo/bar/ arg1 arg2 | flux job id` &&
	echo $jobid > jobname4.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".name == \"\/foo\/bar\/\""
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify job names preserved across restart' '
	jobid1=`cat jobname1.id` &&
	jobid2=`cat jobname2.id` &&
	jobid3=`cat jobname3.id` &&
	jobid4=`cat jobname4.id` &&
	flux job list -s inactive | grep ${jobid1} | jq -e ".name == \"foobar\"" &&
	flux job list -s inactive | grep ${jobid2} | jq -e ".name == \"mycmd\"" &&
	flux job list -s inactive | grep ${jobid3} | jq -e ".name == \"bar\"" &&
	flux job list -s inactive | grep ${jobid4} | jq -e ".name == \"\/foo\/bar\/\""
'

#
# job cwd
#

test_expect_success 'flux job list outputs cwd' '
	pwd=$(pwd) &&
	jobid=`flux submit --wait true | flux job id` &&
	echo $jobid > jobcwd.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".cwd == \"${pwd}\""
'
test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify job cwd preserved across restart' '
	pwd=$(pwd) &&
	jobid=`cat jobcwd.id` &&
	flux job list -s inactive | grep ${jobid} | jq -e ".cwd == \"${pwd}\""
'

#
# job queue
#

test_expect_success 'flux job list output no queue if queue not set' '
	jobid=`flux submit --wait true | flux job id` &&
	echo $jobid > jobqueue1.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".queue == null"
'

test_expect_success 'reconfigure with one queue' '
	flux config load <<-EOT &&
	[queues.foo]
	EOT
	flux queue start --queue=foo
'

test_expect_success 'flux job list outputs queue' '
	jobid=`flux submit --wait --queue=foo true | flux job id` &&
	echo $jobid > jobqueue2.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".queue == \"foo\""
'

test_expect_success 'reconfigure with no queues' '
	flux config load < /dev/null
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify job queue preserved across restart' '
	jobid1=`cat jobqueue1.id` &&
	jobid2=`cat jobqueue2.id` &&
	flux job list -s inactive | grep ${jobid1} | jq -e ".queue == null" &&
	flux job list -s inactive | grep ${jobid2} | jq -e ".queue == \"foo\""
'

#
# job project and bank
#

test_expect_success 'support jobspec updates of project and bank' '
	flux jobtap load --remove=all ${PLUGINPATH}/project-bank-validate.so
'

test_expect_success 'flux job list outputs no project and bank by default' '
	jobid=`flux submit --wait true | flux job id` &&
	echo $jobid > jobprojectbank1.id &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".project == null" &&
	flux job list -s inactive | grep $jobid | jq -e ".bank == null"
'
# initially put job on hold, jobspec-updates don't matter after the job is running
test_expect_success 'flux job list outputs project and bank if one set' '
	jobid=`flux submit --urgency=hold true | flux job id` &&
	echo $jobid > jobprojectbank2.id &&
	flux update $jobid project=foo &&
	flux update $jobid bank=bar &&
	flux job urgency $jobid default &&
	wait_jobid_state $jobid inactive &&
	flux job list -s inactive | grep $jobid | jq -e ".project == \"foo\"" &&
	flux job list -s inactive | grep $jobid | jq -e ".bank == \"bar\""
'
test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify job project and bank preserved across restart' '
	jobid1=`cat jobprojectbank1.id` &&
	jobid2=`cat jobprojectbank2.id` &&
	flux job list -s inactive | grep $jobid1 | jq -e ".project == null" &&
	flux job list -s inactive | grep $jobid1 | jq -e ".bank == null" &&
	flux job list -s inactive | grep $jobid | jq -e ".project == \"foo\"" &&
	flux job list -s inactive | grep $jobid | jq -e ".bank == \"bar\""
'

test_expect_success 'remove jobtap plugins' '
	flux jobtap remove all
'

#
# job task count
#

test_expect_success 'flux job list outputs ntasks correctly (1 task)' '
	jobid=`flux submit --wait hostname | flux job id` &&
	echo $jobid > taskcount1.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 1"
'

test_expect_success 'flux job list outputs ntasks correctly (4 tasks)' '
	jobid=`flux submit --wait -n4 hostname | flux job id` &&
	echo $jobid > taskcount2.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 4"
'

test_expect_success 'flux job list outputs ntasks correctly (4 nodes, 4 tasks)' '
	jobid=`flux submit --wait -N4 -n4 hostname | flux job id` &&
	echo $jobid > taskcount3.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 4"
'

# not-evenly divisible tasks / nodes should force "total" count of tasks in jobspec
test_expect_success 'flux job list outputs ntasks correctly (3 nodes, 4 tasks)' '
	jobid=`flux submit --wait -N3 -n4 hostname | flux job id` &&
	echo $jobid > taskcount4.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 4"
'

test_expect_success 'flux job list outputs ntasks correctly (3 cores)' '
	jobid=`flux submit --wait --cores=3 hostname | flux job id` &&
	echo $jobid > taskcount5.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 3"
'

test_expect_success 'flux job list outputs ntasks correctly (tasks-per-node)' '
	jobid=`flux submit --wait -N2 --tasks-per-node=3 hostname | flux job id` &&
	echo $jobid > taskcount6.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 6"
'

# N.B. As of this test writing, tasks-per-node uses
# per-resource.type=node.  But write more direct test in case of
# future changes.
test_expect_success 'flux job list outputs ntasks correctly (per-resource.type=node)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	extra=$((totalcores / totalnodes + 2)) &&
	jobid=$(flux submit --wait -N ${totalnodes} -n ${totalcores} \
		-o per-resource.type=node \
		-o per-resource.count=${extra} \
		hostname | flux job id) &&
	echo $jobid > taskcount7.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	expected=$((totalnodes * extra)) &&
	echo ${expected} > per_resource_type_node_ntasks.exp &&
	echo $obj | jq -e ".ntasks == ${expected}"
'

test_expect_success 'flux job list outputs ntasks correctly (cores / tasks-per-core)' '
	jobid=`flux submit --wait --cores=4 --tasks-per-core=2 hostname | flux job id` &&
	echo $jobid > taskcount8.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 8"
'

test_expect_success 'flux job list outputs ntasks correctly (tasks / cores-per-task)' '
	jobid=$(flux submit --wait -n2 --cores-per-task=2 \
		-o per-resource.type=core \
		-o per-resource.count=2 \
		hostname | flux job id) &&
	echo $jobid > taskcount9.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == 8"
'

test_expect_success 'flux job list outputs ntasks correctly (nodes / tasks-per-core 2)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	jobid=`flux submit --wait -N ${totalnodes} --tasks-per-core=2 hostname | flux job id` &&
	echo $jobid > taskcount10.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((totalcores * 2)) &&
	echo ${expected} > per_resource_type_core_ntasks1.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ntasks == ${expected}"
'

# N.B. As of this test writing, tasks-per-core uses
# per-resource.type=core.  But write direct test in case of future
# changes.
test_expect_success 'flux job list outputs ntasks correctly (cores / per-resource.type=core)' '
	totalcores=$(flux resource list -s up -no {ncores}) &&
	jobid=$(flux submit --wait --cores=${totalcores} \
		-o per-resource.type=core \
		-o per-resource.count=2 \
		hostname | flux job id) &&
	echo $jobid > taskcount11.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	expected=$((totalcores * 2)) &&
	echo ${expected} > per_resource_type_core_ntasks2.exp &&
	echo $obj | jq -e ".ntasks == ${expected}"
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify task count preserved across restart' '
	jobid1=`cat taskcount1.id` &&
	jobid2=`cat taskcount2.id` &&
	jobid3=`cat taskcount3.id` &&
	jobid4=`cat taskcount4.id` &&
	jobid5=`cat taskcount5.id` &&
	jobid6=`cat taskcount6.id` &&
	jobid7=`cat taskcount7.id` &&
	jobid8=`cat taskcount8.id` &&
	jobid9=`cat taskcount9.id` &&
	jobid10=`cat taskcount10.id` &&
	jobid11=`cat taskcount11.id` &&
	obj=$(flux job list -s inactive | grep ${jobid1}) &&
	echo $obj | jq -e ".ntasks == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid2}) &&
	echo $obj | jq -e ".ntasks == 4" &&
	obj=$(flux job list -s inactive | grep ${jobid3}) &&
	echo $obj | jq -e ".ntasks == 4" &&
	obj=$(flux job list -s inactive | grep ${jobid4}) &&
	echo $obj | jq -e ".ntasks == 4" &&
	obj=$(flux job list -s inactive | grep ${jobid5}) &&
	echo $obj | jq -e ".ntasks == 3" &&
	obj=$(flux job list -s inactive | grep ${jobid6}) &&
	echo $obj | jq -e ".ntasks == 6" &&
	obj=$(flux job list -s inactive | grep ${jobid7}) &&
	expected=$(cat per_resource_type_node_ntasks.exp) &&
	echo $obj | jq -e ".ntasks == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid8}) &&
	echo $obj | jq -e ".ntasks == 8" &&
	obj=$(flux job list -s inactive | grep ${jobid9}) &&
	echo $obj | jq -e ".ntasks == 8" &&
	obj=$(flux job list -s inactive | grep ${jobid10}) &&
	expected=$(cat per_resource_type_core_ntasks1.exp) &&
	echo $obj | jq -e ".ntasks == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid11}) &&
	expected=$(cat per_resource_type_core_ntasks2.exp) &&
	echo $obj | jq -e ".ntasks == ${expected}"
'

#
# job core count
#

test_expect_success 'flux job list outputs ncores correctly (1 task)' '
	jobid=`flux submit --wait -n1 hostname | flux job id` &&
	echo $jobid > corecount1.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 1"
'

test_expect_success 'flux job list outputs ncores correctly (2 tasks)' '
	jobid=`flux submit --wait -n2 hostname | flux job id` &&
	echo $jobid > corecount2.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (1 task, cores-per-task)' '
	jobid=`flux submit --wait -n1 --cores-per-task=2 hostname | flux job id` &&
	echo $jobid > corecount3.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (2 tasks, cores-per-task)' '
	jobid=`flux submit --wait -n2 --cores-per-task=2 hostname | flux job id` &&
	echo $jobid > corecount4.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 4"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 1 task)' '
	jobid=`flux submit --wait -N1 -n1 hostname | flux job id` &&
	echo $jobid > corecount5.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 1"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 2 tasks)' '
	jobid=`flux submit --wait -N1 -n2 hostname | flux job id` &&
	echo $jobid > corecount6.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (2 nodes, 2 tasks)' '
	jobid=`flux submit --wait -N2 -n2 hostname | flux job id` &&
	echo $jobid > corecount7.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 1 task, exclusive)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	corespernode=$((totalcores / totalnodes)) &&
	jobid=`flux submit --wait -N1 -n1 --exclusive hostname | flux job id` &&
	echo $jobid > corecount8.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((corespernode * 1)) &&
	echo ${expected} > ncores_exclusive1.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 2 tasks, exclusive)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	corespernode=$((totalcores / totalnodes)) &&
	jobid=`flux submit --wait -N1 -n2 --exclusive hostname | flux job id` &&
	echo $jobid > corecount9.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((corespernode * 1)) &&
	echo ${expected} > ncores_exclusive2.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

test_expect_success 'flux job list outputs ncores correctly (2 nodes, 2 tasks, exclusive)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	corespernode=$((totalcores / totalnodes)) &&
	jobid=`flux submit --wait -N2 -n2 --exclusive hostname | flux job id` &&
	echo $jobid > corecount10.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((corespernode * 2)) &&
	echo ${expected} > ncores_exclusive3.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 1 task, cores-per-task)' '
	jobid=`flux submit --wait -N1 -n1 --cores-per-task=2 hostname | flux job id` &&
	echo $jobid > corecount11.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 1 task, cores-per-task)' '
	jobid=`flux submit --wait -N1 -n1 --cores-per-task=2 hostname | flux job id` &&
	echo $jobid > corecount12.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (2 nodes, 2 tasks, cores-per-task)' '
	jobid=`flux submit --wait -N2 -n2 --cores-per-task=2 hostname | flux job id` &&
	echo $jobid > corecount13.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 4"
'

test_expect_success 'flux job list outputs ncores correctly (1 core)' '
	jobid=`flux submit --wait --cores=1 hostname | flux job id` &&
	echo $jobid > corecount14.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 1"
'

test_expect_success 'flux job list outputs ncores correctly (2 cores)' '
	jobid=`flux submit --wait --cores=2 hostname | flux job id` &&
	echo $jobid > corecount15.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 1 core)' '
	jobid=`flux submit --wait -N1 --cores=1 hostname | flux job id` &&
	echo $jobid > corecount16.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 1"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 2 cores)' '
	jobid=`flux submit --wait -N1 --cores=2 hostname | flux job id` &&
	echo $jobid > corecount17.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (2 nodes, 2 cores)' '
	jobid=`flux submit --wait -N2 --cores=2 hostname | flux job id` &&
	echo $jobid > corecount18.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == 2"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 1 task, exclusive)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	corespernode=$((totalcores / totalnodes)) &&
	jobid=`flux submit --wait -N1 --cores=1 --exclusive hostname | flux job id` &&
	echo $jobid > corecount19.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((corespernode * 1)) &&
	echo ${expected} > ncores_exclusive4.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

test_expect_success 'flux job list outputs ncores correctly (1 node, 2 tasks, exclusive)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	corespernode=$((totalcores / totalnodes)) &&
	jobid=`flux submit --wait -N1 --cores=2 --exclusive hostname | flux job id` &&
	echo $jobid > corecount20.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((corespernode * 1)) &&
	echo ${expected} > ncores_exclusive5.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

test_expect_success 'flux job list outputs ncores correctly (2 nodes, 2 tasks, exclusive)' '
	totalnodes=$(flux resource list -s up -no {nnodes}) &&
	totalcores=$(flux resource list -s up -no {ncores}) &&
	corespernode=$((totalcores / totalnodes)) &&
	jobid=`flux submit --wait -N2 --cores=2 --exclusive hostname | flux job id` &&
	echo $jobid > corecount21.id &&
	wait_jobid_state $jobid inactive &&
	expected=$((corespernode * 2)) &&
	echo ${expected} > ncores_exclusive6.exp &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

# use flux queue to ensure jobs stay in pending state
test_expect_success 'flux job list lists ncores if pending & tasks specified' '
	flux queue stop &&
	id=$(flux submit -n3 hostname | flux job id) &&
	flux job list -s pending | grep ${id} &&
	flux job list-ids ${id} | jq -e ".ncores == 3" &&
	flux cancel ${id} &&
	flux queue start
'

# use flux queue to ensure jobs stay in pending state
test_expect_success 'flux job list does not list ncores if pending & nodes exclusive' '
	flux queue stop &&
	id=$(flux submit -N1 --exclusive hostname | flux job id) &&
	flux job list -s pending | grep ${id} &&
	flux job list-ids ${id} | jq -e ".ncores == null" &&
	flux cancel ${id} &&
	flux queue start
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify core count preserved across restart' '
	jobid1=`cat corecount1.id` &&
	jobid2=`cat corecount2.id` &&
	jobid3=`cat corecount3.id` &&
	jobid4=`cat corecount4.id` &&
	jobid5=`cat corecount5.id` &&
	jobid6=`cat corecount6.id` &&
	jobid7=`cat corecount7.id` &&
	jobid8=`cat corecount8.id` &&
	jobid9=`cat corecount9.id` &&
	jobid10=`cat corecount10.id` &&
	jobid11=`cat corecount11.id` &&
	jobid12=`cat corecount12.id` &&
	jobid13=`cat corecount13.id` &&
	jobid14=`cat corecount14.id` &&
	jobid15=`cat corecount15.id` &&
	jobid16=`cat corecount16.id` &&
	jobid17=`cat corecount17.id` &&
	jobid18=`cat corecount18.id` &&
	jobid19=`cat corecount19.id` &&
	jobid20=`cat corecount20.id` &&
	jobid21=`cat corecount21.id` &&
	obj=$(flux job list -s inactive | grep ${jobid1}) &&
	echo $obj | jq -e ".ncores == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid2}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid3}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid4}) &&
	echo $obj | jq -e ".ncores == 4" &&
	obj=$(flux job list -s inactive | grep ${jobid5}) &&
	echo $obj | jq -e ".ncores == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid6}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid7}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid8}) &&
	expected=$(cat ncores_exclusive1.exp) &&
	echo $obj | jq -e ".ncores == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid9}) &&
	expected=$(cat ncores_exclusive2.exp) &&
	echo $obj | jq -e ".ncores == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid10}) &&
	expected=$(cat ncores_exclusive3.exp) &&
	echo $obj | jq -e ".ncores == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid11}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid12}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid13}) &&
	echo $obj | jq -e ".ncores == 4" &&
	obj=$(flux job list -s inactive | grep ${jobid14}) &&
	echo $obj | jq -e ".ncores == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid15}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid16}) &&
	echo $obj | jq -e ".ncores == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid17}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid18}) &&
	echo $obj | jq -e ".ncores == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid19}) &&
	expected=$(cat ncores_exclusive4.exp) &&
	echo $obj | jq -e ".ncores == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid20}) &&
	expected=$(cat ncores_exclusive5.exp) &&
	echo $obj | jq -e ".ncores == ${expected}" &&
	obj=$(flux job list -s inactive | grep ${jobid21}) &&
	expected=$(cat ncores_exclusive6.exp) &&
	echo $obj | jq -e ".ncores == ${expected}"
'

#
# job node count
#

test_expect_success 'flux job list outputs nnodes correctly (1 task / 1 node)' '
	jobid=`flux submit --wait -n1 hostname | flux job id` &&
	echo $jobid > nodecount1.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".nnodes == 1"
'

test_expect_success 'flux job list outputs nnodes correctly (2 tasks, / 1 node)' '
	jobid=`flux submit --wait -n2 hostname | flux job id` &&
	echo $jobid > nodecount2.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".nnodes == 1"
'

test_expect_success 'flux job list outputs nnodes correctly (3 tasks, / 2 nodes)' '
	jobid=`flux submit --wait -n3 hostname | flux job id` &&
	echo $jobid > nodecount3.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".nnodes == 2"
'

test_expect_success 'flux job list outputs nnodes correctly (5 tasks, / 3 nodes)' '
	jobid=`flux submit --wait -n5 hostname | flux job id` &&
	echo $jobid > nodecount4.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".nnodes == 3"
'

# use flux queue to ensure jobs stay in pending state
test_expect_success 'flux job list does not list nnodes if no nodes requested' '
	flux queue stop &&
	id=$(flux submit -n1 hostname | flux job id) &&
	flux job list -s pending | grep ${id} &&
	flux job list-ids ${id} | jq -e ".nnodes == null" &&
	flux cancel ${id} &&
	flux queue start
'

# use flux queue to ensure jobs stay in pending state
test_expect_success 'flux job list lists nnodes for pending jobs if nodes requested' '
	flux queue stop &&
	id1=$(flux submit -N1 hostname | flux job id) &&
	id2=$(flux submit -N3 hostname | flux job id) &&
	flux job list -s pending | grep ${id1} &&
	flux job list -s pending | grep ${id2} &&
	flux job list-ids ${id1} | jq -e ".nnodes == 1" &&
	flux job list-ids ${id2} | jq -e ".nnodes == 3" &&
	flux cancel ${id1} ${id2} &&
	flux queue start
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify nnodes preserved across restart' '
	jobid1=`cat nodecount1.id` &&
	jobid2=`cat nodecount2.id` &&
	jobid3=`cat nodecount3.id` &&
	jobid4=`cat nodecount4.id` &&
	obj=$(flux job list -s inactive | grep ${jobid1}) &&
	echo $obj | jq -e ".nnodes == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid2}) &&
	echo $obj | jq -e ".nnodes == 1" &&
	obj=$(flux job list -s inactive | grep ${jobid3}) &&
	echo $obj | jq -e ".nnodes == 2" &&
	obj=$(flux job list -s inactive | grep ${jobid4}) &&
	echo $obj | jq -e ".nnodes == 3"
'

#
# job rank list / nodelist
#

test_expect_success 'flux job list outputs ranks/nodelist correctly (1 node)' '
	jobid=`flux submit --wait -N1 hostname | flux job id` &&
	echo $jobid > nodelist1.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ranks == \"0\"" &&
	nodes=`flux job info $jobid R | flux R decode --nodelist` &&
	echo $obj | jq -e ".nodelist == \"${nodes}\""
'

test_expect_success 'flux job list outputs ranks/nodelist correctly (3 nodes)' '
	jobid=`flux submit --wait -N3 hostname | flux job id` &&
	echo $jobid > nodelist2.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".ranks == \"[0-2]\"" &&
	nodes=`flux job info $jobid R | flux R decode --nodelist` &&
	echo $obj | jq -e ".nodelist == \"${nodes}\""
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify ranks/nodelist preserved across restart' '
	jobid1=`cat nodelist1.id` &&
	jobid2=`cat nodelist2.id` &&
	obj=$(flux job list -s inactive | grep ${jobid1}) &&
	echo $obj | jq -e ".ranks == \"0\"" &&
	nodes=`flux job info ${jobid1} R | flux R decode --nodelist` &&
	echo $obj | jq -e ".nodelist == \"${nodes}\"" &&
	obj=$(flux job list -s inactive | grep ${jobid2}) &&
	echo $obj | jq -e ".ranks == \"[0-2]\"" &&
	nodes=`flux job info ${jobid2} R | flux R decode --nodelist` &&
	echo $obj | jq -e ".nodelist == \"${nodes}\""
'

#
# job success
#

test_expect_success 'flux job list outputs success correctly (true)' '
	jobid=`flux submit --wait hostname | flux job id` &&
	echo $jobid > success1.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".success == true"
'

test_expect_success 'flux job list outputs success correctly (false)' '
	jobid=`flux submit --wait nosuchcommand | flux job id` &&
	echo $jobid > success2.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	echo $obj | jq -e ".success == false"
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify success preserved across restart' '
	jobid1=`cat success1.id` &&
	jobid2=`cat success2.id` &&
	obj=$(flux job list -s inactive | grep ${jobid1}) &&
	test_debug "echo $obj" &&
	echo $obj | jq -e ".success == true" &&
	obj=$(flux job list -s inactive | grep ${jobid2}) &&
	echo $obj | jq -e ".success == false"
'

# job exceptions

test_expect_success 'flux job list outputs exceptions correctly (no exception)' '
	jobid=`flux submit --wait hostname | flux job id` &&
	echo $jobid > exceptions1.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	test_debug "echo $obj" &&
	echo $obj | jq -e ".exception_occurred == false" &&
	echo $obj | jq -e ".exception_severity == null" &&
	echo $obj | jq -e ".exception_type == null" &&
	echo $obj | jq -e ".exception_note == null"
'

test_expect_success 'flux job list outputs exceptions correctly (exception)' '
	jobid=`flux submit --wait nosuchcommand | flux job id` &&
	echo $jobid > exceptions2.id &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	test_debug "echo $obj" &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"exec\"" &&
	echo $obj | jq .exception_note | grep "No such file or directory"
'

# N.B. sleepinf.sh and wait-event on job data to workaround
# rare job startup race.  See #5210
test_expect_success 'flux job list outputs exceptions correctly (exception cancel no message)' '
	jobid=`flux submit ./sleepinf.sh | flux job id` &&
	echo $jobid > exceptions3.id &&
	flux job wait-event -W -p guest.output $jobid data &&
	flux cancel $jobid &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	test_debug "echo $obj" &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"cancel\"" &&
	echo $obj | jq -e ".exception_note == \"\""
'

# N.B. sleepinf.sh and wait-event on job data to workaround
# rare job startup race.  See #5210
test_expect_success 'flux job list outputs exceptions correctly (exception cancel w/ message)' '
	jobid=`flux submit ./sleepinf.sh | flux job id` &&
	echo $jobid > exceptions4.id &&
	flux job wait-event -W -p guest.output $jobid data &&
	flux cancel -m "mecanceled" $jobid &&
	wait_jobid_state $jobid inactive &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	test_debug "echo $obj" &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"cancel\"" &&
	echo $obj | jq -e ".exception_note == \"mecanceled\""
'

# N.B. sleepinf.sh and wait-event on job data to workaround
# rare job startup race.  See #5210
test_expect_success 'flux job list outputs exceptions correctly (user exception)' '
	jobid=`flux submit ./sleepinf.sh | flux job id` &&
	echo $jobid > exceptions5.id &&
	test_debug "echo started $jobid, waiting for first line of output" &&
	flux job wait-event -W -p guest.output $jobid data &&
	test_debug "echo raising user exception foo" &&
	flux job raise --type=foo --severity=0 -m "foobar" $jobid &&
	test_debug "echo waiting for $jobid to become inactive" &&
	wait_jobid_state $jobid inactive &&
	test_debug "flux job list -s inactive | jq" &&
	obj=$(flux job list -s inactive | grep $jobid) &&
	test_debug "echo $obj" &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"foo\"" &&
	echo $obj | jq -e ".exception_note == \"foobar\""
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify exceptions preserved across restart' '
	jobid1=`cat exceptions1.id` &&
	jobid2=`cat exceptions2.id` &&
	jobid3=`cat exceptions3.id` &&
	jobid4=`cat exceptions4.id` &&
	jobid5=`cat exceptions5.id` &&
	obj=$(flux job list -s inactive | grep ${jobid1}) &&
	echo $obj | jq -e ".success == true" &&
	echo $obj | jq -e ".exception_occurred == false" &&
	echo $obj | jq -e ".exception_severity == null" &&
	echo $obj | jq -e ".exception_type == null" &&
	echo $obj | jq -e ".exception_note == null" &&
	obj=$(flux job list -s inactive | grep ${jobid2}) &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"exec\"" &&
	echo $obj | jq .exception_note | grep "No such file or directory" &&
	obj=$(flux job list -s inactive | grep ${jobid3}) &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"cancel\"" &&
	echo $obj | jq -e ".exception_note == \"\"" &&
	obj=$(flux job list -s inactive | grep ${jobid4}) &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"cancel\"" &&
	echo $obj | jq -e ".exception_note == \"mecanceled\"" &&
	obj=$(flux job list -s inactive | grep ${jobid5}) &&
	echo $obj | jq -e ".exception_occurred == true" &&
	echo $obj | jq -e ".exception_severity == 0" &&
	echo $obj | jq -e ".exception_type == \"foo\"" &&
	echo $obj | jq -e ".exception_note == \"foobar\""
'

# expiration time

test_expect_success 'flux job list outputs expiration time when set' '
	jobid=$(flux submit -t 500s sleep 1000 | flux job id) &&
	echo $jobid > expiration.id &&
	fj_wait_event $jobid start &&
	flux job list | grep $jobid > expiration.json &&
	test_debug "cat expiration.json" &&
	jq -e ".expiration > now" < expiration.json &&
	flux cancel $jobid
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify expiration time preserved across restart' '
	jobid=`cat expiration.id` &&
	flux job list -s inactive | grep ${jobid} > expiration2.json &&
	jq -e ".expiration > now" < expiration2.json
'

# duration time

test_expect_success 'flux job list outputs duration time when set' '
	jobid=$(flux submit -t 60m sleep 1000 | flux job id) &&
	echo $jobid > duration.id &&
	fj_wait_event $jobid start &&
	flux job list | grep $jobid > duration.json &&
	test_debug "cat duration.json" &&
	jq -e ".duration == 3600.0" < duration.json &&
	flux cancel $jobid
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_job_list_ready
'

test_expect_success 'verify duration preserved across restart' '
	jobid=`cat duration.id` &&
	flux job list -s inactive | grep ${jobid} > duration2.json &&
	jq -e ".duration == 3600.0" < duration2.json
'

# all job attributes

# note that not all attributes may be returned by via the 'all'
# attribute.  e.g. exception data won't be returned for a job that
# doesn't have an exception, no annotations if none set, etc.
#
# so we check for all the core / expected attributes for the situation

test_expect_success 'list request with all attr works (job success)' '
	flux run hostname &&
	jq -j -c -n  "{max_entries:1, attrs:[\"all\"]}" \
	  | $RPC_STREAM job-list.list | jq ".jobs[0]" > all_success.out &&
	cat all_success.out | jq -e ".id" &&
	cat all_success.out | jq -e ".userid" &&
	cat all_success.out | jq -e ".urgency" &&
	cat all_success.out | jq -e ".priority" &&
	cat all_success.out | jq -e ".t_submit" &&
	cat all_success.out | jq -e ".t_depend" &&
	cat all_success.out | jq -e ".t_run" &&
	cat all_success.out | jq -e ".t_cleanup" &&
	cat all_success.out | jq -e ".t_inactive" &&
	cat all_success.out | jq -e ".state" &&
	cat all_success.out | jq -e ".name" &&
	cat all_success.out | jq -e ".ntasks" &&
	cat all_success.out | jq -e ".nnodes" &&
	cat all_success.out | jq -e ".ranks" &&
	cat all_success.out | jq -e ".nodelist" &&
	cat all_success.out | jq -e ".success == true" &&
	cat all_success.out | jq -e ".exception_occurred == false" &&
	cat all_success.out | jq -e ".result" &&
	cat all_success.out | jq -e ".waitstatus" &&
	cat all_success.out | jq -e ".expiration"
'

test_expect_success 'list request with all attr works (job fail)' '
	! flux run -N1000 -n1000 hostname &&
	jq -j -c -n  "{max_entries:1, attrs:[\"all\"]}" \
	  | $RPC_STREAM job-list.list | jq ".jobs[0]" > all_fail.out &&
	cat all_fail.out | jq -e ".id" &&
	cat all_fail.out | jq -e ".userid" &&
	cat all_fail.out | jq -e ".urgency" &&
	cat all_fail.out | jq -e ".priority" &&
	cat all_fail.out | jq -e ".t_submit" &&
	cat all_fail.out | jq -e ".t_depend" &&
	cat all_fail.out | jq -e ".t_cleanup" &&
	cat all_fail.out | jq -e ".t_inactive" &&
	cat all_fail.out | jq -e ".state" &&
	cat all_fail.out | jq -e ".name" &&
	cat all_fail.out | jq -e ".ntasks" &&
	cat all_fail.out | jq -e ".success == false" &&
	cat all_fail.out | jq -e ".exception_occurred == true" &&
	cat all_fail.out | jq -e ".exception_type" &&
	cat all_fail.out | jq -e ".exception_severity" &&
	cat all_fail.out | jq -e ".exception_note" &&
	cat all_fail.out | jq -e ".result"
'

#
# max comparison
#

test_expect_success 'default comparison can get all of the jobs' '
	flux job list -A > /dev/null
'

test_expect_success 'remove job list module' '
	flux module remove job-list
'

test_expect_success 'job-list: invalid max_comparisons leads to error' '
	cat >joblist.toml <<EOF &&
[job-list]
max_comparisons = -1
EOF
	flux config reload &&
	test_must_fail flux module load job-list
'

test_expect_success 'job-list: config low max_comparisons' '
	cat >joblist.toml <<EOF &&
[job-list]
max_comparisons = 10
EOF
	flux config reload &&
	flux module load job-list
'

test_expect_success 'flux job list fails on low comparison count' '
	test_must_fail flux job list -A > /dev/null 2> comparisons.err &&
	grep "Excessive comparisons" comparisons.err
'

test_expect_success 'job-list: update config with invalid input fails' '
	test_must_fail flux config load <<-EOF
[job-list]
max_comparisons = -1
EOF
'

test_expect_success 'job-list: update config bigger max_comparisons' '
	flux config load <<-EOF
[job-list]
max_comparisons = 10000
EOF
'

test_expect_success 'flux job list works again' '
	flux job list -A > /dev/null
'

test_expect_success 'job-list: update config no comparison limit' '
	flux config load <<-EOF
[job-list]
max_comparisons = 0
EOF
'

test_expect_success 'flux job list works' '
	flux job list -A > /dev/null
'

#
# job-list can handle flux-restart events
#
# TODO: presently job-list depends on job-manager journal, so it is
# not possible to test the reload of the job-manager that doesn't also
# reload job-list.
#

wait_jobid() {
	local jobid="$1"
	local i=0
	while ! flux job list --states=sched | grep $jobid > /dev/null \
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

# to ensure jobs are still in PENDING state, stop queue before
# reloading job-list & job-manager.	 reload job-exec & sched-simple
# after wait_jobid, b/c we do not want the job to be accidentally
# executed.
test_expect_success 'job-list parses flux-restart events' '
	flux queue stop &&
	jobid=`flux submit hostname | flux job id` &&
	fj_wait_event $jobid priority &&
	flux module unload job-list &&
	flux module reload job-manager &&
	flux module load job-list &&
	wait_jobid $jobid &&
	flux module reload job-exec &&
	flux module reload sched-simple &&
	flux queue start
'

#
# jobspec-update event testing
#

# N.B. "defaultqueue" will have jobs "submitted" to it, but updates
# make it appear that jobs were not submitted to it.  "nosubmitqueue"
# will never have jobs submitted to it.
test_expect_success 'configure update queues' '
	flux config load <<-EOT &&
	[queues.defaultqueue]
	[queues.updatequeue]
	[queues.nosubmitqueue]
	EOT
	flux queue start --all
'

wait_id_inactive() {
	id=$1
	local i=0
	while ! flux job list --states=inactive | grep ${id} > /dev/null \
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

test_expect_success 'load jobspec-update test plugin' '
        flux jobtap load --remove=all ${PLUGINPATH}/jobspec-update-job-list.so
'

test_expect_success 'run job in the default queue' '
	flux submit -q defaultqueue --wait true | flux job id > update1.id &&
	wait_id_inactive $(cat update1.id)
'

# jobspec-update-job-list changes command to "hostname" (thus job name
# changes), duration to 1000.0, queue to "updatequeue".
test_expect_success 'job-list returns expected jobspec changes' '
	flux job list -s inactive | grep $(cat update1.id) | jq -e ".name == \"hostname\"" &&
	flux job list -s inactive | grep $(cat update1.id) | jq -e ".queue == \"updatequeue\"" &&
	flux job list -s inactive | grep $(cat update1.id) | jq -e ".duration == 1000.0"
'

#
# "nosubmitqueue" never has job submitted to it, make sure stats
# string is non-empty and filled with zeros.
#
test_expect_success 'job stats lists jobs in correct state in each queue' '
	defaultq=`flux job stats | jq ".queues[] | select( .name == \"defaultqueue\" )"` &&
	updateq=`flux job stats | jq ".queues[] | select( .name == \"updatequeue\" )"` &&
	nosubmitq=`flux job stats | jq ".queues[] | select( .name == \"nosubmitqueue\" )"` &&
	test -n "$nosubmitq" &&
	echo $defaultq | jq -e ".job_states.depend == 0" &&
	echo $defaultq | jq -e ".job_states.priority == 0" &&
	echo $defaultq | jq -e ".job_states.sched == 0" &&
	echo $defaultq | jq -e ".job_states.run == 0" &&
	echo $defaultq | jq -e ".job_states.cleanup == 0" &&
	echo $defaultq | jq -e ".job_states.inactive == 0" &&
	echo $defaultq | jq -e ".job_states.total == 0" &&
	echo $defaultq | jq -e ".successful == 0" &&
	echo $defaultq | jq -e ".failed == 0" &&
	echo $defaultq | jq -e ".canceled == 0" &&
	echo $defaultq | jq -e ".timeout == 0" &&
	echo $defaultq | jq -e ".inactive_purged == 0" &&
	echo $updateq | jq -e ".job_states.depend == 0" &&
	echo $updateq | jq -e ".job_states.priority == 0" &&
	echo $updateq | jq -e ".job_states.sched == 0" &&
	echo $updateq | jq -e ".job_states.run == 0" &&
	echo $updateq | jq -e ".job_states.cleanup == 0" &&
	echo $updateq | jq -e ".job_states.inactive == 1" &&
	echo $updateq | jq -e ".job_states.total == 1" &&
	echo $updateq | jq -e ".successful == 1" &&
	echo $updateq | jq -e ".failed == 0" &&
	echo $updateq | jq -e ".canceled == 0" &&
	echo $updateq | jq -e ".timeout == 0" &&
	echo $updateq | jq -e ".inactive_purged == 0" &&
	echo $nosubmitq | jq -e ".job_states.depend == 0" &&
	echo $nosubmitq | jq -e ".job_states.priority == 0" &&
	echo $nosubmitq | jq -e ".job_states.sched == 0" &&
	echo $nosubmitq | jq -e ".job_states.run == 0" &&
	echo $nosubmitq | jq -e ".job_states.cleanup == 0" &&
	echo $nosubmitq | jq -e ".job_states.inactive == 0" &&
	echo $nosubmitq | jq -e ".job_states.total == 0" &&
	echo $nosubmitq | jq -e ".successful == 0" &&
	echo $nosubmitq | jq -e ".failed == 0" &&
	echo $nosubmitq | jq -e ".canceled == 0" &&
	echo $nosubmitq | jq -e ".timeout == 0" &&
	echo $nosubmitq | jq -e ".inactive_purged == 0"
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_id_inactive $(cat update1.id)
'

test_expect_success 'job-list returns expected jobspec changes after reload' '
	flux job list -s inactive | grep $(cat update1.id) | jq -e ".name == \"hostname\"" &&
	flux job list -s inactive | grep $(cat update1.id) | jq -e ".queue == \"updatequeue\"" &&
	flux job list -s inactive | grep $(cat update1.id) | jq -e ".duration == 1000.0"
'

#
# After reload, job-list will not have ever seen any jobs submitted to
# "defaultqueue", need to check that stats string is non-empty and all
# stats are 0.
#
test_expect_success 'job stats in each queue correct after reload' '
	defaultq=`flux job stats | jq ".queues[] | select( .name == \"defaultqueue\" )"` &&
	updateq=`flux job stats | jq ".queues[] | select( .name == \"updatequeue\" )"` &&
	test -n "$defaultq" &&
	test -n "$nosubmitq" &&
	echo $defaultq | jq -e ".job_states.depend == 0" &&
	echo $defaultq | jq -e ".job_states.priority == 0" &&
	echo $defaultq | jq -e ".job_states.sched == 0" &&
	echo $defaultq | jq -e ".job_states.run == 0" &&
	echo $defaultq | jq -e ".job_states.cleanup == 0" &&
	echo $defaultq | jq -e ".job_states.inactive == 0" &&
	echo $defaultq | jq -e ".job_states.total == 0" &&
	echo $defaultq | jq -e ".successful == 0" &&
	echo $defaultq | jq -e ".failed == 0" &&
	echo $defaultq | jq -e ".canceled == 0" &&
	echo $defaultq | jq -e ".timeout == 0" &&
	echo $defaultq | jq -e ".inactive_purged == 0" &&
	echo $updateq | jq -e ".job_states.depend == 0" &&
	echo $updateq | jq -e ".job_states.priority == 0" &&
	echo $updateq | jq -e ".job_states.sched == 0" &&
	echo $updateq | jq -e ".job_states.run == 0" &&
	echo $updateq | jq -e ".job_states.cleanup == 0" &&
	echo $updateq | jq -e ".job_states.inactive == 1" &&
	echo $updateq | jq -e ".job_states.total == 1" &&
	echo $updateq | jq -e ".successful == 1" &&
	echo $updateq | jq -e ".failed == 0" &&
	echo $updateq | jq -e ".canceled == 0" &&
	echo $updateq | jq -e ".timeout == 0" &&
	echo $updateq | jq -e ".inactive_purged == 0" &&
	echo $nosubmitq | jq -e ".job_states.depend == 0" &&
	echo $nosubmitq | jq -e ".job_states.priority == 0" &&
	echo $nosubmitq | jq -e ".job_states.sched == 0" &&
	echo $nosubmitq | jq -e ".job_states.run == 0" &&
	echo $nosubmitq | jq -e ".job_states.cleanup == 0" &&
	echo $nosubmitq | jq -e ".job_states.inactive == 0" &&
	echo $nosubmitq | jq -e ".job_states.total == 0" &&
	echo $nosubmitq | jq -e ".successful == 0" &&
	echo $nosubmitq | jq -e ".failed == 0" &&
	echo $nosubmitq | jq -e ".canceled == 0" &&
	echo $nosubmitq | jq -e ".timeout == 0" &&
	echo $nosubmitq | jq -e ".inactive_purged == 0"
'

test_expect_success 'remove jobtap plugins and remove queue config' '
	flux jobtap remove all &&
	flux config load < /dev/null
'

#
# resource-update event testing
#

# the resource-update-expiration plugin will add 60 minutes to the
# expiration time of a job
# N.B. in future, may wish to do test via a `flux update` or similar tool
test_expect_success 'support resource-update event' '
	flux jobtap load --remove=all ${PLUGINPATH}/resource-update-expiration.so
'

test_expect_success 'run a job' '
	flux submit --time-limit=5m --wait-event=start sleep 300 | flux job id > rupdate1.id
'

# expiration should be 65m out, make sure it is > 10m, i.e. not 5m
test_expect_success 'expiration is in the future' '
	current=$(date +%s) &&
	testexp=`expr $current + 10 \* 60` &&
	echo $testexp > test_expiration.out &&
	flux job list -s active | grep $(cat rupdate1.id) | jq -e ".expiration > $(cat test_expiration.out)"
'

test_expect_success 'cancel job' '
	flux cancel $(cat rupdate1.id)
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_id_inactive $(cat rupdate1.id)
'

test_expect_success 'job-list returns expected resource changes after reload' '
	flux job list -s inactive | grep $(cat rupdate1.id) | jq -e ".expiration > $(cat test_expiration.out)"
'

test_expect_success 'remove jobtap plugins' '
        flux jobtap remove all
'

#
# job list special cases
#

test_expect_success 'list count / max_entries works' '
	count=`flux job list -s inactive -c 0 | wc -l` &&
	test $count -gt 5 &&
	count=`flux job list -s inactive -c 5 | wc -l` &&
	test $count -eq 5
'

# List of all job attributes (XXX: maybe this should be pulled in from somewhere
#  else? E.g. documentation?

JOB_ATTRIBUTES="\
userid \
urgency \
priority \
t_submit \
t_depend \
t_run \
t_cleanup \
t_inactive \
state \
name \
cwd \
queue \
project \
bank \
ntasks \
ncores \
duration \
nnodes \
ranks \
nodelist \
success \
exception_occurred \
exception_type \
exception_severity \
exception_note \
result \
expiration \
annotations \
waitstatus \
dependencies \
all
"

test_expect_success 'list request with empty attrs works' '
	id=$(id -u) &&
	jq -j -c -n  "{max_entries:5, userid:${id}, states:0, results:0, attrs:[]}" \
	  | $RPC_STREAM job-list.list > list_empty_attrs.out &&
	for attr in $JOB_ATTRIBUTES; do
	test_must_fail grep $attr list_empty_attrs.out
	done
'
test_expect_success 'list request with excessive max_entries works' '
	id=$(id -u) &&
	jq -j -c -n  "{max_entries:100000, userid:${id}, states:0, results:0, attrs:[]}" \
	  | $RPC_STREAM job-list.list
'

# list-attrs also lists the special attribute 'all'
LIST_ATTRIBUTES="${JOB_ATTRIBUTES} all"

test_expect_success 'list-attrs works' '
	$RPC job-list.list-attrs < /dev/null > list_attrs.out &&
	for attr in $LIST_ATTRIBUTES; do
	grep $attr list_attrs.out
	done
'


#
# stress test
#

wait_jobs_finish() {
	local i=0
	while ([ "$(flux job list | wc -l)" != "0" ]) \
		  && [ $i -lt 1000 ]
	do
		sleep 0.1
		i=$((i + 1))
	done
	if [ "$i" -eq "1000" ]
	then
		return 1
	fi
	return 0
}

test_expect_success LONGTEST 'stress job-list.list-id' '
	flux python ${FLUX_SOURCE_DIR}/t/job-list/list-id.py 500 &&
	wait_jobs_finish
'

#
#
# stats tests
#


test_expect_success 'job-list stats works' '
	flux module stats --parse jobs.pending job-list &&
	flux module stats --parse jobs.running job-list &&
	flux module stats --parse jobs.inactive job-list &&
	flux module stats --parse idsync.lookups job-list &&
	flux module stats --parse idsync.waits job-list
'

test_expect_success 'configure batch,debug queues' '
	flux config load <<-EOT &&
	[queues.batch]
	[queues.debug]
	EOT
	flux queue start --all
'

test_expect_success 'run some jobs in the batch,debug queues' '
	flux submit -q batch --wait true | flux job id > stats1.id &&
	flux submit -q debug --wait true | flux job id > stats2.id &&
	flux submit -q batch --wait false | flux job id > stats3.id &&
	flux submit -q debug --wait false | flux job id > stats4.id &&
	wait_id_inactive $(cat stats1.id) &&
	wait_id_inactive $(cat stats2.id) &&
	wait_id_inactive $(cat stats3.id) &&
	wait_id_inactive $(cat stats4.id)
'

test_expect_success 'job stats lists jobs in correct state in each queue' '
	batchq=`flux job stats | jq ".queues[] | select( .name == \"batch\" )"` &&
	debugq=`flux job stats | jq ".queues[] | select( .name == \"debug\" )"` &&
	echo $batchq | jq -e ".job_states.depend == 0" &&
	echo $batchq | jq -e ".job_states.priority == 0" &&
	echo $batchq | jq -e ".job_states.sched == 0" &&
	echo $batchq | jq -e ".job_states.run == 0" &&
	echo $batchq | jq -e ".job_states.cleanup == 0" &&
	echo $batchq | jq -e ".job_states.inactive == 2" &&
	echo $batchq | jq -e ".job_states.total == 2" &&
	echo $batchq | jq -e ".successful == 1" &&
	echo $batchq | jq -e ".failed == 1" &&
	echo $batchq | jq -e ".canceled == 0" &&
	echo $batchq | jq -e ".timeout == 0" &&
	echo $batchq | jq -e ".inactive_purged == 0" &&
	echo $debugq | jq -e ".job_states.depend == 0" &&
	echo $debugq | jq -e ".job_states.priority == 0" &&
	echo $debugq | jq -e ".job_states.sched == 0" &&
	echo $debugq | jq -e ".job_states.run == 0" &&
	echo $debugq | jq -e ".job_states.cleanup == 0" &&
	echo $debugq | jq -e ".job_states.inactive == 2" &&
	echo $debugq | jq -e ".job_states.total == 2" &&
	echo $debugq | jq -e ".successful == 1" &&
	echo $debugq | jq -e ".failed == 1" &&
	echo $debugq | jq -e ".canceled == 0" &&
	echo $debugq | jq -e ".timeout == 0" &&
	echo $debugq | jq -e ".inactive_purged == 0"
'

test_expect_success 'reload the job-list module' '
	flux module reload job-list &&
	wait_id_inactive $(cat stats1.id) &&
	wait_id_inactive $(cat stats2.id) &&
	wait_id_inactive $(cat stats3.id) &&
	wait_id_inactive $(cat stats4.id)
'

test_expect_success 'job stats in each queue correct after reload' '
	batchq=`flux job stats | jq ".queues[] | select( .name == \"batch\" )"` &&
	debugq=`flux job stats | jq ".queues[] | select( .name == \"debug\" )"` &&
	echo $batchq | jq -e ".job_states.depend == 0" &&
	echo $batchq | jq -e ".job_states.priority == 0" &&
	echo $batchq | jq -e ".job_states.sched == 0" &&
	echo $batchq | jq -e ".job_states.run == 0" &&
	echo $batchq | jq -e ".job_states.cleanup == 0" &&
	echo $batchq | jq -e ".job_states.inactive == 2" &&
	echo $batchq | jq -e ".job_states.total == 2" &&
	echo $batchq | jq -e ".successful == 1" &&
	echo $batchq | jq -e ".failed == 1" &&
	echo $batchq | jq -e ".canceled == 0" &&
	echo $batchq | jq -e ".timeout == 0" &&
	echo $batchq | jq -e ".inactive_purged == 0" &&
	echo $debugq | jq -e ".job_states.depend == 0" &&
	echo $debugq | jq -e ".job_states.priority == 0" &&
	echo $debugq | jq -e ".job_states.sched == 0" &&
	echo $debugq | jq -e ".job_states.run == 0" &&
	echo $debugq | jq -e ".job_states.cleanup == 0" &&
	echo $debugq | jq -e ".job_states.inactive == 2" &&
	echo $debugq | jq -e ".job_states.total == 2" &&
	echo $debugq | jq -e ".successful == 1" &&
	echo $debugq | jq -e ".failed == 1" &&
	echo $debugq | jq -e ".canceled == 0" &&
	echo $debugq | jq -e ".timeout == 0" &&
	echo $debugq | jq -e ".inactive_purged == 0"
'

wait_total() {
	total=$1
	local i=0
	while ! flux job stats | jq -e ".job_states.total == ${total}" \
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

# purge all jobs except two, the remaining two should be the failed
# jobs submitted to the batch and debug queues.
test_expect_success 'job-stats correct after purge' '
	total=$(flux job stats | jq .job_states.total) &&
	purged=$((total - 2)) &&
	flux job purge --force --num-limit=2 &&
	wait_total 2 &&
	flux job stats | jq -e ".job_states.inactive == 2" &&
	flux job stats | jq -e ".job_states.total == 2" &&
	flux job stats | jq -e ".inactive_purged == ${purged}" &&
	batchq=`flux job stats | jq ".queues[] | select( .name == \"batch\" )"` &&
	debugq=`flux job stats | jq ".queues[] | select( .name == \"debug\" )"` &&
	echo $batchq | jq -e ".job_states.inactive == 1" &&
	echo $batchq | jq -e ".job_states.total == 1" &&
	echo $batchq | jq -e ".successful == 0" &&
	echo $batchq | jq -e ".failed == 1" &&
	echo $batchq | jq -e ".inactive_purged == 1" &&
	echo $debugq | jq -e ".job_states.inactive == 1" &&
	echo $debugq | jq -e ".job_states.total == 1" &&
	echo $debugq | jq -e ".successful == 0" &&
	echo $debugq | jq -e ".failed == 1" &&
	echo $debugq | jq -e ".inactive_purged == 1"
'

test_expect_success 'remove queues' '
	flux config load < /dev/null
'

#
# corner case tests
#

test_expect_success 'list request with empty payload fails with EPROTO(71)' '
	${RPC_STREAM} job-list.list 71 </dev/null
'
test_expect_success 'list request with invalid input fails with EPROTO(71) (attrs not an array)' '
	jq -j -c -n  "{max_entries:5, attrs:5}" \
	  | $RPC_STREAM job-list.list 71 "invalid payload: attrs must be an array"
'
test_expect_success 'list request with invalid input fails with EINVAL(22) (attrs non-string)' '
	jq -j -c -n  "{max_entries:5, attrs:[5]}" \
	  | $RPC_STREAM job-list.list 22 "attr has no string value"
'
test_expect_success 'list request with invalid input fails with EINVAL(22) (attrs illegal field)' '
	jq -j -c -n  "{max_entries:5, attrs:[\"foo\"]}" \
	  | $RPC_STREAM job-list.list 22 "foo is not a valid attribute"
'
test_expect_success 'list-id request with empty payload fails with EPROTO(71)' '
	${RPC} job-list.list-id 71 </dev/null
'
test_expect_success 'list-id request with invalid input fails with EPROTO(71) (attrs not an array)' '
	id=`flux submit hostname | flux job id` &&
	jq -j -c -n  "{id:${id}, attrs:5}" \
	  | $RPC job-list.list-id 71 "invalid payload: attrs must be an array"
'
test_expect_success 'list-id request with invalid input fails with EINVAL(22) (attrs non-string)' '
	id=$(flux jobs -c1 -ano {id.dec}) &&
	jq -j -c -n  "{id:${id}, attrs:[5]}" \
	  | $RPC job-list.list-id 22 "attr has no string value"
'
test_expect_success 'list-id request with invalid input fails with EINVAL(22) (attrs illegal field)' '
	id=$(flux jobs -c1 -ano {id.dec}) &&
	jq -j -c -n  "{id:${id}, attrs:[\"foo\"]}" \
	  | $RPC job-list.list-id 22 "foo is not a valid attribute"
'
# Restart job-list from the KVS.  job-list used to pull in job eventlogs
# directly from the KVS at startup, but now it gets them via the job manager
# journal.  Therefore we need to restart the job-manager too, which requires
# some other things to be restarted to get a functional system.
restart_from_kvs() {
	flux module remove job-list
	flux module reload job-manager
	flux module reload -f sched-simple
	flux module reload -f job-exec
	flux module load job-list &&
	wait_job_list_ready
}

# N.B. we remove annotations from the alloc event in this test, but it could
# be cached and replayed via the job-manager, so we need to reload it
# and associated modules too
test_expect_success 'job-list can handle events missing optional data (alloc)' '
	userid=`id -u` &&
	cat <<EOF >eventlog_empty_alloc.out &&
{"timestamp":1000.0,"name":"submit","context":{"userid":${userid},"urgency":16,"flags":0,"version":1}}
{"timestamp":1001.0,"name":"validate"}
{"timestamp":1002.0,"name":"depend"}
{"timestamp":1003.0,"name":"priority","context":{"priority":8}}
{"timestamp":1004.0,"name":"alloc","context":{}}
{"timestamp":1005.0,"name":"start"}
{"timestamp":1006.0,"name":"finish","context":{"status":0}}
{"timestamp":1007.0,"name":"release","context":{"ranks":"all","final":true}}
{"timestamp":1008.0,"name":"free"}
{"timestamp":1009.0,"name":"clean"}
EOF
	jobid=`flux submit --wait hostname` &&
	kvspath=`flux job id --to=kvs ${jobid}` &&
	flux kvs put -r ${kvspath}.eventlog=- < eventlog_empty_alloc.out &&
	restart_from_kvs &&
	flux job list-ids ${jobid} > empty_alloc.out &&
	cat empty_alloc.out | jq -e ".annotations == null"
'
# N.B. Note the original job was submitted with urgency 16, but we
# hard code 8 in the fake eventlog.	 This is just to make sure the fake
# eventlog was loaded correctly at the end of the test.
#
# N.B. We add extra events into this fake eventlog for testing
test_expect_success 'job-list can handle events with superfluous context data' '
	userid=`id -u` &&
	cat <<EOF >eventlog_superfluous_context.out &&
{"timestamp":1000.0,"name":"submit","context":{"userid":${userid},"urgency":8,"flags":0,"version":1,"etc":1}}
{"timestamp":1001.0,"name":"dependency-add","context":{"description":"begin-time=1234.000","etc":1}}
{"timestamp":1002.0,"name":"validate","context":{"etc":1}}
{"timestamp":1003.0,"name":"dependency-remove","context":{"description":"begin-time=1234.000","etc":1}}
{"timestamp":1004.0,"name":"depend","context":{"etc":1}}
{"timestamp":1005.0,"name":"priority","context":{"priority":8,"etc":1}}
{"timestamp":1006.0,"name":"alloc","context":{"annotations":{"sched":{"resource_summary":"rank0/core0"}},"etc":1}}
{"timestamp":1007.0,"name":"prolog-start","context":{"description":"job-manager.prolog","etc":1}}
{"timestamp":1008.0,"name":"prolog-finish","context":{"description":"job-manager.prolog","status":0,"etc":1}}
{"timestamp":1009.0,"name":"start","context":{"etc":1}}
{"timestamp":1010.0,"name":"finish","context":{"status":0,"etc":1}}
{"timestamp":1011.0,"name":"epilog-start","context":{"description":"job-manager.epilog","etc":1}}
{"timestamp":1012.0,"name":"release","context":{"ranks":"all","final":true,"etc":1}}
{"timestamp":1013.0,"name":"epilog-finish","context":{"description":"job-manager.epilog","status":0,"etc":1}}
{"timestamp":1014.0,"name":"free","context":{"etc":1}}
{"timestamp":1015.0,"name":"clean","context":{"etc":1}}
EOF
	jobid=`flux submit --wait --urgency=default hostname` &&
	kvspath=`flux job id --to=kvs ${jobid}` &&
	flux kvs put -r ${kvspath}.eventlog=- < eventlog_superfluous_context.out &&
	restart_from_kvs &&
	flux job list-ids ${jobid} > superfluous_context.out &&
	cat superfluous_context.out | jq -e ".urgency == 8"
'

# invalid job data tests
#
# to avoid potential racyness, wait up to 5 seconds for job to appear
# in job list.	Note that we can't use `flux job list-ids`, b/c we
# need specific job states to be crossed to ensure the job-list module
# has processed the invalid data.
#
# In addition, note that the tests below are specific to how the data
# is invalid in these tests and how job-list parses the invalid data.
# Different parsing errors could have some fields initialized but
# others not.
#
# note that these tests should be done last, as the introduction of
# invalid job data into the KVS could affect tests above.
#

# Following tests use invalid / unusual jobspecs, must load a more permissive validator

ingest_module ()
{
	cmd=$1; shift
	flux module ${cmd} job-ingest $*
}

test_expect_success 'reload job-ingest without validator' '
	ingest_module reload disable-validator
'

test_expect_success 'create illegal jobspec with empty command array' '
	flux submit --dry-run hostname > hostname.json &&
	cat hostname.json | jq ".tasks[0].command = []" > bad_jobspec.json
'

# note that ntasks will not be set if jobspec invalid
test_expect_success 'flux job list works on job with illegal jobspec' '
	jobid=`flux job submit bad_jobspec.json | flux job id` &&
	fj_wait_event $jobid clean >/dev/null &&
	i=0 &&
	while ! flux job list --states=inactive | grep $jobid > /dev/null \
		   && [ $i -lt 5 ]
	do
		sleep 1
		i=$((i + 1))
	done &&
	test "$i" -lt "5" &&
	flux job list --states=inactive | grep $jobid > list_illegal_jobspec.out &&
	cat list_illegal_jobspec.out | jq -e ".name == null" &&
	cat list_illegal_jobspec.out | jq -e ".ntasks == null"
'

test_expect_success NO_CHAIN_LINT 'flux job list-ids works on job with illegal jobspec' '
	${RPC} job-list.job-state-pause 0 </dev/null
	jobid=`flux job submit bad_jobspec.json | flux job id`
	fj_wait_event $jobid clean >/dev/null
	flux job list-ids ${jobid} > list_id_illegal_jobspec.out &
	pid=$!
	wait_idsync 1 &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	wait $pid &&
	cat list_id_illegal_jobspec.out | jq -e ".id == ${jobid}"
'

test_expect_success 'create jobspec with missing cwd' '
	cat hostname.json | jq "del(.attributes.system.cwd)" > no_cwd_jobspec.json
'

test_expect_success 'flux job list works on job with no cwd' '
	jobid=`flux job submit no_cwd_jobspec.json | flux job id` &&
	fj_wait_event $jobid clean >/dev/null &&
	i=0 &&
	while ! flux job list --states=inactive | grep $jobid > /dev/null \
		   && [ $i -lt 5 ]
	do
		sleep 1
		i=$((i + 1))
	done &&
	test "$i" -lt "5" &&
	flux job list --states=inactive | grep $jobid > list_no_cwd_jobspec.out &&
	cat list_no_cwd_jobspec.out | jq -e ".cwd == null"
'

test_expect_success 'reload job-ingest with defaults' '
	ingest_module reload
'

test_expect_success 'flux job list works on racy annotations' '
	${RPC} job-list.job-state-pause 0 </dev/null &&
	jobid=`flux submit --wait hostname | flux job id` &&
	${RPC} job-list.job-state-unpause 0 </dev/null &&
	i=0 &&
	while ! flux job list --states=inactive | grep $jobid > /dev/null \
		   && [ $i -lt 5 ]
	do
		sleep 1
		i=$((i + 1))
	done &&
	test "$i" -lt "5"  &&
	flux job list --states=inactive | grep $jobid > list_racy_annotation.out &&
	cat list_racy_annotation.out | jq -e ".annotations"
'

test_expect_success 'flux job list subcommands are not displayed in help' '
	test_must_fail flux job -h 2>job-help.err &&
	test_must_fail grep "^[ ]*list" job-help.err
'
test_expect_success 'flux job list subcommands fail when stdout is a tty' '
	test_must_fail $runpty --stderr=tty1.err flux job list &&
	grep "This is not the command" tty1.err &&
	test_must_fail $runpty --stderr=tty2.err flux job list-inactive &&
	grep "This is not the command" tty2.err &&
	test_must_fail $runpty --stderr=tty3.err flux job list-ids &&
	grep "This is not the command" tty3.err
'

test_done
