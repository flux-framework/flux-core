#!/bin/sh

test_description='Test job manager jobtap plugin interface'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

id_byname() {
	flux jobs -ano {name}:{id} | grep ^$1: | head -1 | cut -d: -f2
}

test_expect_success 'job-manager: builtin module loaded by default' '
	flux jobtap list > list.out &&
	test_debug "cat list.out" &&
	grep "builtin.priority.default" list.out
'
test_expect_success 'job-manager: attempt to load invalid plugin fails' '
	test_must_fail flux jobtap load builtin.foo &&
	flux jobtap list >list2.out &&
	test_debug "cat list2.out" &&
	grep "default" list2.out
'
test_expect_success 'job-manager: load with invalid conf fails' '
	cat <<-EOF >badconf.py &&
	import flux
	h = flux.Flux()
	print(h.rpc("job-manager.jobtap", {"load": "none", "conf": 1}).get())
	EOF
	test_must_fail flux python badconf.py
'
test_expect_success 'job-manager: multiple plugins can be loaded' '
	flux jobtap load ${PLUGINPATH}/args.so &&
	flux jobtap load ${PLUGINPATH}/test.so &&
	flux jobtap list > plugins &&
	grep args plugins &&
	grep test plugins &&
	flux jobtap remove args &&
	flux jobtap remove test
'
test_expect_success 'job-manager: plugins can be loaded by configuration' '
	mkdir testconf &&
	cat <<-EOF >testconf/job-manager.toml &&
	[job-manager]
	plugins = [
	  { remove = "all" },
	  { load = "${PLUGINPATH}/test.so" },
	  { load = "${PLUGINPATH}/args.so" },
	]
	EOF
	flux start -o,-c $(pwd)/testconf flux jobtap list > confplugins.out &&
	test_debug "cat confplugins.out" &&
	grep args confplugins.out &&
	grep test confplugins.out
'
test_expect_success 'job-manager: default plugin sets priority to urgency' '
	jobid=$(flux mini submit --urgency=8 hostname) &&
	flux job wait-event -v $jobid priority &&
	test $(flux jobs -no {priority} $jobid) = 8 &&
	flux job wait-event -v $jobid clean
'
test_expect_success 'job-manager: default plugin works with sched.prioritize' '
	ncores=$(flux resource list -s free -no {ncores}) &&
	allcores=$(flux mini submit -n ${ncores} sleep 1000) &&
	flux mini submit --cc=1-2 --flags=debug hostname >prio.jobids &&
	job1=$(cat prio.jobids | head -1) &&
	job3=$(cat prio.jobids | tail -1) &&
	flux job wait-event -v $job1 debug.alloc-request &&
	test_debug "echo updating urgency of top queued job" &&
	flux job urgency $job1 17 &&
	flux job wait-event -v $job1 priority &&
	test_debug "echo updating urgency of last queued job" &&
	flux job urgency $job3 18 &&
	flux job wait-event -v $job3 debug.alloc-request &&
	test_debug "echo cleanup" &&
	flux job cancelall -f &&
	flux queue drain
'
test_expect_success 'job-manager: no plugin acts the same as default' '
	flux jobtap remove all &&
	flux queue stop &&
	jobid=$(flux mini submit --urgency=22 hostname) &&
	flux job wait-event -v $jobid priority &&
	test $(flux jobs -no {priority} $jobid) = 22 &&
	flux job urgency $jobid 7 &&
	flux job wait-event --count=2 -v $jobid priority &&
	test $(flux jobs -no {priority} $jobid) = 7 &&
	flux queue start &&
	flux job wait-event $jobid clean
'
test_expect_success HAVE_JQ 'job-manager: builtin hold plugin holds jobs' '
	flux jobtap load --remove=all builtin.priority.hold &&
	flux mini bulksubmit --job-name=cc-{0} hostname ::: $(seq 1 4) \
	    >hold.jobids &&
	flux job wait-event -v $(cat hold.jobids | tail -1) priority &&
	for id in $(cat hold.jobids); do
	    test_debug "echo waiting for job ${id} priority event" &&
	    flux job wait-event -f json ${id} priority | \
	        jq -e ".context.priority == 0"
	done
'
test_expect_success 'job-manager: job is released when urgency set' '
	jobid=$(id_byname cc-1) &&
	test_debug "echo jobid=${jobid}" &&
	state=$(flux jobs -no {state} $jobid) &&
	test_debug "echo state is ${state}" &&
	test "$state" = "SCHED" &&
	flux job urgency $jobid 1 &&
	flux job wait-event -v -t 5 $jobid clean
'
test_expect_success 'job-manager: cancel of held job works' '
	jobid=$(id_byname cc-2) &&
	flux job cancel $jobid &&
	flux job wait-event -v -t 5 $jobid clean
'
test_expect_success 'job-manager: add administrative hold to one job' '
	jobid=$(id_byname cc-3) &&
	flux job urgency $jobid 0 &&
	state=$(flux jobs -no {state} $jobid) &&
	test_debug "echo state is ${state}" &&
	test "$state" = "SCHED"
'
test_expect_success 'job-manager: held jobs get a priority on plugin load' '
	flux jobtap load --remove=all builtin.priority.default &&
	jobid=$(id_byname cc-4) &&
	flux job wait-event -v -t 5 $jobid clean
'
test_expect_success 'job-manager: but adminstratively held job is still held' '
	jobid=$(id_byname cc-3) &&
	state=$(flux jobs -no {state} $jobid) &&
	priority=$(flux jobs -no {priority} $jobid) &&
	urgency=$(flux jobs -no {urgency} $jobid) &&
	test_debug "echo state=${state} pri=${priority} urgency=${urgency}" &&
	test "$state" = "SCHED" &&
	test $priority = 0 &&
	test $urgency  = 0
'
test_expect_success 'job-manager: release final held job' '
	jobid=$(id_byname cc-3) &&
	flux job urgency $jobid 1 &&
	flux job wait-event -v $jobid clean
'
test_expect_success 'job-manager: test with random priority plugin' '
	flux module reload sched-simple mode=unlimited &&
	ncores=$(flux resource list -s free -no {ncores}) &&
	sleepjob=$(flux mini submit -n ${ncores} sleep 3000) &&
	flux jobtap load --remove=all ${PLUGINPATH}/random.so &&
	flux mini bulksubmit --flags waitable --job-name=random-{} hostname \
	    ::: $(seq 1 4) &&
	flux jobs -c4 -no {name}:{priority} | sort > pri-before.out &&
	sleep 1 &&
	flux jobs -c4 -no {name}:{priority} | sort > pri-after.out &&
	test_must_fail test_cmp pri-before.out pri-after.out &&
	flux job cancel $sleepjob &&
	flux job wait-event -vt 30 $sleepjob clean &&
	flux job wait --all -v
'
test_expect_success 'job-manager: run args test plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/args.so &&
	flux mini run hostname &&
	flux dmesg | grep args-check > args-check.log &&
	test_debug "cat args-check.log" &&
	test $(grep -c OK args-check.log) = 8
'
test_expect_success 'job-manager: load test jobtap plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/test.so foo.test=1 &&
	flux dmesg | grep "conf={\"foo\":{\"test\":1}}"
'
test_expect_success 'job-manager: run all test plugin test modes' '
	cat <<-EOF | sort >test-modes.txt &&
	priority unset
	callback error
	priority type error
	sched: priority unavail
	sched: update priority
	sched: dependency-add
	annotations error
	EOF
	COUNT=$(cat test-modes.txt | wc -l) &&
	run_timeout 20 \
	  flux mini bulksubmit -q --watch \
	    --setattr=system.jobtap.test-mode={} \
	    hostname :::: test-modes.txt >test-plugin.out &&
	test_debug "cat test-plugin.out" &&
	test $COUNT = $(cat test-plugin.out | wc -l) &&
	flux jobs -ac $COUNT -no {annotations.test} | \
	    sort >test-annotations.out &&
	test_cmp test-modes.txt test-annotations.out
'
test_expect_success 'job-manager: run test plugin modes for priority.get' '
	cat <<-EOF | sort >test-modes.priority.get &&
	priority.get: fail
	priority.get: unavail
	priority.get: bad arg
	EOF
	cat <<-EOT >reprioritize.py &&
	import flux
	flux.Flux().rpc("job-manager.test.reprioritize", {}).get()
	EOT
	COUNT=$(cat test-modes.priority.get|wc -l) &&
	flux queue stop &&
	flux mini bulksubmit \
	    --flags waitable --setattr=system.jobtap.test-mode={} hostname \
	    :::: test-modes.priority.get > priority.get.jobids &&
	flux python ./reprioritize.py &&
	flux queue start &&
	test_debug "flux dmesg | grep jobtap\.test" &&
	run_timeout 20 flux job wait -v --all
'
test_expect_success 'job-manager: run test plugin modes for job.validate' '
	test_expect_code 1 \
	    flux mini submit\
	        --setattr=system.jobtap.test-mode="validate failure" \
	        hostname >validate-failure.out 2>&1 &&
	test_debug "cat validate-failure.out" &&
	grep "rejected for testing" validate-failure.out &&
	test_expect_code 1 \
	    flux mini submit\
	        --setattr=system.jobtap.test-mode="validate failure nullmsg" \
	        hostname >validate-failure2.out 2>&1 &&
	test_debug "cat validate-failure2.out" &&
	grep "rejected by job-manager plugin" validate-failure2.out &&
	test_expect_code 1 \
	    flux mini submit\
	        --setattr=system.jobtap.test-mode="validate failure nomsg" \
	        hostname >validate-failure3.out 2>&1 &&
	test_debug "cat validate-failure3.out" &&
	grep "rejected by job-manager plugin" validate-failure3.out
'
test_expect_success 'job-manager: plugin can keep job in PRIORITY state' '
	flux jobtap load --remove=all ${PLUGINPATH}/priority-wait.so &&
	jobid=$(flux mini submit hostname) &&
	flux job wait-event -vt 5 $jobid depend &&
	test $(flux jobs -no {state} $jobid) = "PRIORITY"
'
test_expect_success 'job-manager: job exits PRIORITY when priority is set' '
	jobid=$(flux jobs -nc 1 -o {id}) &&
	cat <<-EOF >pri-set.py &&
	import flux
	from flux.job import JobID
	import sys

	jobid = flux.job.JobID(sys.argv[1])
	priority = int(sys.argv[2])
	topic = "job-manager.priority-wait.release"
	print(flux.Flux().rpc(topic, {"id": jobid, "priority": priority}).get())
	EOF
	flux python pri-set.py $jobid 42000 &&
	flux job wait-event -vt 5 $jobid clean &&
	flux jobs -no {priority} $jobid &&
	test $(flux jobs -no {priority} $jobid) = 42000
'
test_expect_success 'job-manager: plugin can reject some jobs in a batch' '
	flux module reload job-ingest batch-count=6 &&
	flux jobtap load --remove=all ${PLUGINPATH}/validate.so &&
	test_expect_code 1 \
	    flux mini bulksubmit --watch \
	        --setattr=system.jobtap.validate-test-id={} \
	        echo foo ::: 1 1 1 4 4 1 >validate-plugin.out 2>&1 &&
	test_debug "cat validate-plugin.out" &&
	grep "Job had reject_id" validate-plugin.out &&
	test 4 -eq $(grep -c foo validate-plugin.out)
'
test_expect_success 'job-manager: plugin can manage depedencies' '
	cat <<-EOF >dep-remove.py &&
	import flux
	from flux.job import JobID
	import sys

	jobid = flux.job.JobID(sys.argv[1])
	topic = "job-manager.dependency-test.remove"
	payload = {"id": jobid, "description": "dependency-test"}
	print(flux.Flux().rpc(topic, payload).get())
	EOF
	flux module reload job-ingest &&
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so &&
	jobid=$(flux mini submit --dependency=test:dependency-test hostname) &&
	flux job wait-event -vt 15 ${jobid} dependency-add &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-remove.py ${jobid} &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'job-manager: job.state.depend is called on plugin load' '
	jobid=$(flux mini submit --dependency=test:dependency-test hostname) &&
	flux job wait-event -vt 15 ${jobid} dependency-add &&
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-remove.py ${jobid} &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_done
