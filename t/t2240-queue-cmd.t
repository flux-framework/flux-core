#!/bin/sh
test_description='Test flux queue command'

. $(dirname $0)/util/wait-util.sh

. $(dirname $0)/sharness.sh

test_under_flux 1 full -Slog-stderr-level=1

LIST_JOBS=${FLUX_BUILD_DIR}/t/job-manager/list-jobs

test_expect_success 'flux-queue: unknown sub-command fails with usage message' '
	test_must_fail flux queue wrongsubcmd 2>usage.out &&
	grep -i usage: usage.out
'

test_expect_success 'flux-queue: missing sub-command fails with usage message' '
	test_must_fail flux queue 2>usage2.out &&
	grep -i usage: usage2.out
'

test_expect_success 'flux-queue: drain with extra free args fails' '
	test_must_fail flux queue drain xyz 2>usage6.out &&
	grep -i usage: usage6.out
'

test_expect_success 'flux-queue: idle with extra free args fails' '
	test_must_fail flux queue idle xyz 2>usage7.out &&
	grep -i usage: usage7.out
'

test_expect_success 'flux-queue: status with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue status
'

test_expect_success 'flux-queue: disable with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue disable foo
'

test_expect_success 'flux-queue: enable with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue enable
'

test_expect_success 'flux-queue: drain with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue drain
'

test_expect_success 'flux-queue: idle with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue idle
'

test_expect_success 'flux-queue: disable works' '
	flux queue disable --message="system is fubar" > disable.out &&
	grep "^Job submission is disabled" disable.out &&
	grep "system is fubar" disable.out
'

test_expect_success 'flux-queue: job submit fails with queue disabled' '
	test_must_fail flux run true
'

test_expect_success 'flux-queue: enable works' '
	flux queue enable > enable.out &&
	grep "^Job submission is enabled" enable.out
'

test_expect_success 'flux-queue: flux run works after enable' '
	run_timeout 60 flux run true
'

test_expect_success 'flux-queue: stop with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue stop
'

test_expect_success 'flux-queue: start with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue start
'

test_expect_success 'flux-queue: stop works' '
	flux queue stop --message="my unique message" > stop.out &&
	grep "^Scheduling is stopped" stop.out &&
	grep "my unique message" stop.out
'

test_expect_success 'flux-queue: status reports reason for stop' '
	flux queue status >status.out &&
	cat <<-EOT >status.exp &&
	Job submission is enabled
	Scheduling is stopped: my unique message
	EOT
	test_cmp status.exp status.out
'

test_expect_success 'flux-queue: stop works without message' '
	flux queue stop > stop2.out &&
	grep "^Scheduling is stopped" stop2.out
'

test_expect_success 'flux-queue: status reports no reason for stop' '
	flux queue status >status2.out &&
	cat <<-EOT >status2.exp &&
	Job submission is enabled
	Scheduling is stopped
	EOT
	test_cmp status2.exp status2.out
'

test_expect_success 'flux-queue: stop with --nocheckpoint works' '
	flux start \
	    -Scontent.dump=dump_queue_nocheckpoint1.tar \
	    flux queue stop &&
	tar -xvf dump_queue_nocheckpoint1.tar &&
	cat checkpoint/job-manager | jq -e ".queue[0].start == false" &&
	flux start \
	    -Scontent.dump=dump_queue_nocheckpoint2.tar \
	    flux queue stop --nocheckpoint &&
	tar -xvf dump_queue_nocheckpoint2.tar &&
	cat checkpoint/job-manager | jq -e ".queue[0].start == true"
'

test_expect_success 'flux-queue: submit some jobs' '
	flux submit --cc 1-3 --wait-event=priority true
'

test_expect_success 'flux-queue: start scheduling' '
	flux queue start > start.out &&
	grep "^Scheduling is started" start.out
'

test_expect_success 'flux-queue: queue empties out' '
	run_timeout 60 flux queue drain
'

test_expect_success 'flux-queue: start long job that uses all cores' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	id=$(flux submit -n ${ncores} sleep 600) &&
	flux job wait-event ${id} start &&
	echo ${id} >longjob
'

test_expect_success 'flux-queue: submit a job and make sure alloc sent' '
	id=$(flux submit --flags debug true) &&
	flux job wait-event ${id} debug.alloc-request
'

wait_for_pending_cancel () {
	local n=$1
	local count=$2
	for try in $(seq 1 $n); do
		echo Check queue pending count, try ${try} of $n 2>&1
		flux queue status -v 2>&1 \
		  | grep "${count} alloc requests pending to scheduler" \
		  && return
	done
}

# internally cancel is sent to scheduler, can be racy when scheduler responds
# to cancel request, must wait for it to be returned
test_expect_success 'flux-queue: stop canceled alloc request' '
	flux queue stop &&
	wait_for_pending_cancel 10 0
'

test_expect_success 'flux-queue: start scheduling and cancel long job' '
	flux queue start &&
	flux cancel $(cat longjob)
'

test_expect_success 'flux-queue: queue empties out' '
	flux queue drain
'

test_expect_success 'flux-queue: unload scheduler' '
	flux module remove sched-simple
'

test_expect_success 'flux-queue: submit a job to ping scheduler' '
	flux submit --flags debug true
'

wait_for_sched_offline() {
	local n=$1
	for try in $(seq 1 $n); do
		echo Check queue status for offline, try ${try} of $n 2>&1
		flux queue status 2>&1 | grep stopped && return
	done
}

test_expect_success 'flux-queue: queue says scheduling stopped' '
	wait_for_sched_offline 10 &&
	flux queue status >sched_stat.out &&
	cat <<-EOT >sched_stat.exp &&
	Job submission is enabled
	Scheduling is stopped: Scheduler is offline
	EOT
	test_cmp sched_stat.exp sched_stat.out
'

test_expect_success 'flux-queue: queue contains 1 active job' '
	COUNT=$(${LIST_JOBS} | wc -l) &&
	test ${COUNT} -eq 1
'

test_expect_success 'flux-queue: load scheduler' '
	flux module load sched-simple mode=limited=1
'

test_expect_success 'flux-queue: queue says scheduling is enabled' '
	flux queue status >sched_stat2.out &&
	cat <<-EOT >sched_stat2.exp &&
	Job submission is enabled
	Scheduling is started
	EOT
	test_cmp sched_stat2.exp sched_stat2.out
'

test_expect_success 'flux-queue: job in queue ran' '
	run_timeout 30 flux queue drain
'

test_expect_success 'flux-queue: submit a long job that uses all cores' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	flux submit -n ${ncores} sleep 600
'

test_expect_success 'flux-queue: submit 2 more jobs' '
	flux submit true &&
	flux submit true
'

test_expect_success 'flux-queue: there are 3 active jobs' '
	COUNT=$(${LIST_JOBS} | wc -l) &&
	test ${COUNT} -eq 3
'

test_expect_success 'flux-queue: queue status -v shows expected counts' '
	flux queue status -v >stat.out &&
	cat <<-EOT >stat.exp &&
	Job submission is enabled
	Scheduling is started
	1 alloc requests queued
	1 alloc requests pending to scheduler
	1 running jobs
	EOT
	test_cmp stat.exp stat.out
'

test_expect_success 'flux-queue: stop queue and cancel long job' '
	flux queue stop &&
	flux cancel --all -S RUN
'

test_expect_success 'flux-queue: queue becomes idle' '
	run_timeout 30 flux queue idle
'

test_expect_success 'flux-queue: queue status -v shows expected counts' '
	flux queue status -v >stat2.out &&
	cat <<-EOT >stat2.exp &&
	Job submission is enabled
	Scheduling is stopped
	0 alloc requests queued
	0 alloc requests pending to scheduler
	0 running jobs
	EOT
	test_cmp stat2.exp stat2.out
'

test_expect_success 'flux-queue: start queue and drain' '
	flux queue start &&
	run_timeout 30 flux queue drain
'

test_expect_success 'flux-queue: quiet option works' '
	flux queue disable --quiet -m foof > disable_quiet.out &&
	test_must_fail grep submission disable_quiet.out &&
	flux queue enable --quiet > enable_quiet.out &&
	test_must_fail grep submission enable_quiet.out &&
	flux queue stop --quiet > stop_quiet.out &&
	test_must_fail grep Scheduling stop_quiet.out &&
	flux queue start --quiet > start_quiet.out &&
	test_must_fail grep Scheduling start_quiet.out
'

test_expect_success 'flux-queue: verbose option works' '
	flux queue disable --verbose > disable_verbose.out &&
	test $(grep -c "submission is disabled" disable_verbose.out) -eq 1 &&
	flux queue enable --verbose > enable_verbose.out &&
	test $(grep -c "submission is enabled" enable_verbose.out) -eq 1 &&
	flux queue stop --verbose > stop_verbose.out &&
	test $(grep -c "Scheduling is stopped" stop_verbose.out) -eq 1 &&
	flux queue start --verbose > start_verbose.out &&
	test $(grep -c "Scheduling is started" start_verbose.out) -eq 1
'

runas_guest() {
	local userid=$(($(id -u)+1))
	FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

test_expect_success 'flux-queue: status allowed for guest' '
	runas_guest flux queue status
'

test_expect_success 'flux-queue: stop denied for guest' '
	test_must_fail runas_guest flux queue stop 2>guest_stop.err &&
	grep "requires owner credentials" guest_stop.err
'

test_expect_success 'flux-queue: start denied for guest' '
	test_must_fail runas_guest flux queue start 2>guest_start.err &&
	grep "requires owner credentials" guest_start.err
'

test_expect_success 'flux-queue: disable denied for guest' '
	test_must_fail runas_guest flux queue disable 2>guest_dis.err &&
	grep "requires owner credentials" guest_dis.err
'

test_expect_success 'flux-queue: enable denied for guest' '
	test_must_fail runas_guest flux queue enable 2>guest_ena.err &&
	grep "requires owner credentials" guest_ena.err
'

test_expect_success 'flux-queue: drain denied for guest' '
	test_must_fail runas_guest flux queue drain 2>guest_drain.err &&
	grep "requires owner credentials" guest_drain.err
'

test_expect_success 'flux-queue: idle denied for guest' '
	test_must_fail runas_guest flux queue idle 2>guest_idle.err &&
	grep "requires owner credentials" guest_idle.err
'

#
# Test support for named queues
#

test_expect_success 'flux queue status --queue fails with no queues' '
	test_must_fail flux queue status --queue=batch
'
test_expect_success 'flux queue enable --queue fails with no queues' '
	test_must_fail flux queue enable --queue=batch
'
test_expect_success 'flux queue disable --queue fails with no queues' '
	test_must_fail flux queue disable --queue=batch
'
test_expect_success 'flux queue start --queue fails with no queues' '
	test_must_fail flux queue start --queue=batch
'
test_expect_success 'flux queue stop --queue fails with no queues' '
	test_must_fail flux queue stop --queue=batch
'
test_expect_success 'flux queue status queue fails with no queues' '
	test_must_fail flux queue status batch
'
test_expect_success 'flux queue enable queue fails with no queues' '
	test_must_fail flux queue enable batch
'
test_expect_success 'flux queue disable queue fails with no queues' '
	test_must_fail flux queue disable batch
'
test_expect_success 'flux queue start queue fails with no queues' '
	test_must_fail flux queue start batch
'
test_expect_success 'flux queue stop queue fails with no queues' '
	test_must_fail flux queue stop batch
'

test_expect_success 'ensure instance is drained' '
	flux queue drain &&
	flux queue status -v
'
test_expect_success 'configure batch,debug queues' '
	flux config load <<-EOT
	[queues.batch]
	[queues.debug]
	EOT
'
test_expect_success 'queues enabled and stopped by default' '
	flux queue status >mqstatus_default.out &&
	test $(grep -c "submission is enabled" mqstatus_default.out) -eq 2 &&
	test $(grep -c "Scheduling is stopped" mqstatus_default.out) -eq 2
'
test_expect_success 'start queues' '
	flux queue start --all &&
	flux queue status >mqstatus_initial.out &&
	test $(grep -c "Scheduling is started" mqstatus_initial.out) -eq 2
'
test_expect_success 'jobs may be submitted to either queue' '
	flux submit -q batch true &&
	flux submit -q debug true
'
test_expect_success 'flux-queue status can show one queue' '
	flux queue status debug >mqstatus_debug.out &&
	test_must_fail grep batch mqstatus_debug.out
'
test_expect_success 'flux-queue disable without queue or --all fails' '
	test_must_fail flux queue disable --message="test reasons"
'
test_expect_success 'flux-queue disable --all affects all queues' '
	flux queue disable --all --message="test reasons" > mqdisable.out &&
	test $(grep -c "submission is disabled: test reason" mqdisable.out) -eq 2 &&
	flux queue status >mqstatus_dis.out &&
	test $(grep -c "submission is disabled: test reason" mqstatus_dis.out) -eq 2
'
test_expect_success 'jobs may not be submitted to either queue' '
	test_must_fail flux submit -q batch true &&
	test_must_fail flux submit -q debug true
'
test_expect_success 'flux-queue enable without queue or --all fails' '
	test_must_fail flux queue enable
'
test_expect_success 'flux-queue enable --all affects all queues' '
	flux queue enable -a > mqenable.out &&
	test $(grep -c "submission is enabled" mqenable.out) -eq 2 &&
	flux queue status >mqstatus_ena.out &&
	test $(grep -c "submission is enabled" mqstatus_ena.out) -eq 2
'
test_expect_success 'flux-queue disable can do one queue' '
	flux queue disable --message=nobatch batch > mqdisable_batch.out &&
	test $(grep -c "submission is" mqdisable_batch.out) -eq 1 &&
	test $(grep -c "submission is disabled: nobatch" mqdisable_batch.out) -eq 1 &&
	flux queue status >mqstatus_batchdis.out &&
	test $(grep -c "submission is enabled" mqstatus_batchdis.out) -eq 1 &&
	test $(grep -c "submission is disabled: nobatch" mqstatus_batchdis.out) -eq 1 &&
	test_must_fail flux submit -q batch true &&
	flux submit -q debug true
'
test_expect_success 'flux-queue enable can do one queue' '
	flux queue enable batch > mqenable_batch.out &&
	test $(grep -c "submission is" mqenable_batch.out) -eq 1 &&
	test $(grep -c "submission is enabled" mqenable_batch.out) -eq 1 &&
	flux queue status >mqstatus_batchena.out &&
	test $(grep -c "submission is enabled" mqstatus_batchena.out) -eq 2 &&
	flux submit -q batch true &&
	flux submit -q debug true
'


test_expect_success 'flux-queue stop --all affects all queues' '
	flux queue stop --all -m "test reasons"> mqstop.out &&
	test $(grep -c "Scheduling is stopped: test reasons" mqstop.out) -eq 2 &&
	flux queue status >mqstatus_stop.out &&
	test $(grep -c "Scheduling is stopped: test reasons" mqstatus_stop.out) -eq 2
'
test_expect_success 'flux-queue stop with multiple queues fails with warning' '
	flux queue start --all &&
	test_must_fail flux queue stop 2>mqstatus_stop2.err &&
	grep "Named queues" mqstatus_stop2.err
'
test_expect_success 'stop queues' '
	flux queue stop --all
'
test_expect_success 'jobs may be submitted to either queue' '
	flux submit --wait-event=priority -q batch true > job_batch1.id &&
	flux submit --wait-event=priority -q debug true > job_debug1.id
'

wait_state() {
	local jobid=$1
	local state=$2
	wait_util "[ \"\$(flux jobs -no {state} ${jobid})\" = \"${state}\" ]"
}

test_expect_success 'submitted jobs are not running' '
	wait_state $(cat job_batch1.id) SCHED &&
	flux jobs -n -o "{state}" $(cat job_batch1.id) | grep SCHED &&
	wait_state $(cat job_debug1.id) SCHED &&
	flux jobs -n -o "{state}" $(cat job_debug1.id) | grep SCHED
'
test_expect_success 'flux-queue start --all affects all queues' '
	flux queue start -a > mqstart.out &&
	test $(grep -c "Scheduling is started" mqstart.out) -eq 2 &&
	flux queue status >mqstatus_start.out &&
	test $(grep -c "Scheduling is started" mqstatus_start.out) -eq 2
'
test_expect_success 'submitted jobs ran and completed' '
	wait_state $(cat job_batch1.id) INACTIVE &&
	flux jobs -n -o "{state}" $(cat job_batch1.id) | grep INACTIVE &&
	wait_state $(cat job_debug1.id) INACTIVE &&
	flux jobs -n -o "{state}" $(cat job_debug1.id) | grep INACTIVE
'
test_expect_success 'flux-queue start with multiple queues emits warning' '
	flux queue stop --all &&
	test_must_fail flux queue start 2>mqstatus_start2.err &&
	grep "Named queues" mqstatus_start2.err
'
test_expect_success 'start all queues' '
	flux queue start --all
'
test_expect_success 'flux-queue stop can do one queue' '
	flux queue stop --message="nobatch" batch > mqstop_batch.out &&
	test $(grep -c "Scheduling is" mqstop_batch.out) -eq 1 &&
	test $(grep -c "Scheduling is stopped: nobatch" mqstop_batch.out) -eq 1 &&
	flux queue status >mqstatus_batchstop.out &&
	test $(grep -c "Scheduling is started" mqstatus_batchstop.out) -eq 1 &&
	test $(grep -c "Scheduling is stopped: nobatch" mqstatus_batchstop.out) -eq 1 &&
	flux submit -q batch true > job_batch2.id &&
	flux submit -q debug true > job_debug2.id
'
test_expect_success 'check one job ran, other job didnt' '
	wait_state $(cat job_debug2.id) INACTIVE &&
	flux jobs -n -o "{state}" $(cat job_debug2.id) | grep INACTIVE &&
	wait_state $(cat job_batch2.id) SCHED &&
	flux jobs -n -o "{state}" $(cat job_batch2.id) | grep SCHED
'
test_expect_success 'flux-queue start can do one queue' '
	flux queue start batch > mqstart_batch.out &&
	test $(grep -c "Scheduling is" mqstart_batch.out) -eq 1 &&
	test $(grep -c "Scheduling is started" mqstart_batch.out) -eq 1 &&
	flux queue status >mqstatus_batchstart.out &&
	test $(grep -c "Scheduling is started" mqstatus_batchstart.out) -eq 2
'
test_expect_success 'previously submitted job run to completion' '
	wait_state $(cat job_batch2.id) INACTIVE &&
	flux jobs -n -o "{state}" $(cat job_batch2.id) | grep INACTIVE
'

test_expect_success 'flux-queue: stop with named queues and --nocheckpoint works' '
	mkdir -p conf.d &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.batch]
	EOT
	cat >stopqueues.sh <<-EOT &&
	#!/bin/sh
	flux queue start --all
	flux queue stop --all
	EOT
	cat >stopqueuesnocheckpoint.sh <<-EOT &&
	#!/bin/sh
	flux queue start --all
	flux queue stop --all --nocheckpoint
	EOT
	chmod +x ./stopqueues.sh &&
	chmod +x ./stopqueuesnocheckpoint.sh &&
	flux start --config-path=$(pwd)/conf.d \
	    -Scontent.dump=dump_queue_named_nocheckpoint1.tar \
	    ./stopqueues.sh &&
	tar -xvf dump_queue_named_nocheckpoint1.tar &&
	cat checkpoint/job-manager | jq -e ".queue[0].start == false" &&
	cat checkpoint/job-manager | jq -e ".queue[1].start == false" &&
	flux start --config-path=$(pwd)/conf.d \
	    -Scontent.dump=dump_queue_named_nocheckpoint2.tar \
	    ./stopqueuesnocheckpoint.sh &&
	tar -xvf dump_queue_named_nocheckpoint2.tar &&
	cat checkpoint/job-manager | jq -e ".queue[0].start == true" &&
	cat checkpoint/job-manager | jq -e ".queue[1].start == true"
'

test_expect_success 'flux-queue: quiet option works with one queue' '
	flux queue disable --quiet -m foof batch > mqdisable_quiet.out &&
	test_must_fail grep submission mqdisable_quiet.out &&
	flux queue enable --quiet batch > mqenable_quiet.out &&
	test_must_fail grep submission mqenable_quiet.out &&
	flux queue stop --quiet batch > mqstop_quiet.out &&
	test_must_fail grep Scheduling mqstop_quiet.out &&
	flux queue start --quiet batch > mqstart_quiet.out &&
	test_must_fail grep Scheduling mqstart_quiet.out
'

test_expect_success 'flux-queue: verbose option works with one queue' '
	flux queue disable --verbose -m foof batch > mqdisable_verbose.out &&
	test $(grep -c "submission is disabled" mqdisable_verbose.out) -eq 1 &&
	test $(grep -c "submission is enabled" mqdisable_verbose.out) -eq 1 &&
	flux queue enable --verbose batch > mqenable_verbose.out &&
	test $(grep -c "submission is enabled" mqenable_verbose.out) -eq 2 &&
	flux queue stop --verbose batch > mqstop_verbose.out &&
	test $(grep -c "Scheduling is stopped" mqstop_verbose.out) -eq 1 &&
	test $(grep -c "Scheduling is started" mqstop_verbose.out) -eq 1 &&
	flux queue start --verbose batch > mqstart_verbose.out &&
	test $(grep -c "Scheduling is started" mqstart_verbose.out) -eq 2
'

test_expect_success 'flux-queue start fails on unknown queue' '
	test_must_fail flux queue start notaqueue
'
test_expect_success 'flux-queue stop fails on unknown queue' '
	test_must_fail flux queue stop notaqueue
'
test_expect_success 'flux-queue enable fails on unknown queue' '
	test_must_fail flux queue enable notaqueue
'
test_expect_success 'flux-queue disable fails on unknown queue' '
	test_must_fail flux queue disable notaqueue
'
test_expect_success 'flux-queue status fails on unknown queue' '
	test_must_fail flux queue status notaqueue
'

test_done
