#!/bin/sh

test_description='Test job manager jobtap plugin interface'

. `dirname $0`/job-manager/sched-helper.sh

. $(dirname $0)/sharness.sh

mkdir -p config

test_under_flux 4 job --config-path=$(pwd)/config -Slog-stderr-level=1

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

id_byname() {
	flux jobs -ano {name}:{id} | grep ^$1: | head -1 | cut -d: -f2
}

test_expect_success 'job-manager: attempt to load invalid plugin fails' '
	flux jobtap list >list1.out &&
	test_must_fail flux jobtap load builtin.foo &&
	flux jobtap list >list2.out &&
	test_debug "cat list2.out" &&
	test_cmp list1.out list2.out
'
test_expect_success 'job-manager: loading invalid builtin plugin fails' '
	test_must_fail flux jobtap load .foo
'
test_expect_success 'job-manager: builtin plugin can be removed' '
	flux jobtap remove .history &&
	flux jobtap list >list-nohist.out &&
	test_must_fail grep ^\.history list-nohist.out &&
	flux jobtap load .history
'
test_expect_success 'job-manager: load with invalid conf fails' '
	cat <<-EOF >badconf.py &&
	import flux
	h = flux.Flux()
	print(h.rpc("job-manager.jobtap", {"load": "none", "conf": 1}).get())
	EOF
	test_must_fail flux python badconf.py
'
test_expect_success 'job-manager: jobtap remove with invalid plugin fails' '
	test_must_fail flux jobtap remove notfound.so
'
test_expect_success 'job-manager: jobtap remove all does not error ' '
	flux jobtap remove all
'
test_expect_success 'job-manager: multiple plugins can be loaded' '
	flux jobtap load ${PLUGINPATH}/args.so &&
	flux jobtap load ${PLUGINPATH}/test.so &&
	flux jobtap list > plugins &&
	grep args plugins &&
	grep test plugins &&
	flux jobtap remove args* &&
	flux jobtap remove test*
'
test_expect_success 'job-manager: loading duplicate plugins fails' '
	flux jobtap load ${PLUGINPATH}/args.so &&
	test_must_fail flux jobtap load ${PLUGINPATH}/args.so >dup.log 2>&1 &&
	test_debug "cat dup.log" &&
	grep "already loaded" dup.log &&
	flux jobtap remove args.so
'
test_expect_success 'job-manager: query of plugin works' '
	flux jobtap load ${PLUGINPATH}/test.so &&
	flux jobtap query test.so >query.json &&
	test_debug "jq -S . <query.json" &&
	jq -e ".name == \"test.so\"" <query.json &&
	jq -e ".path == \"${PLUGINPATH}/test.so\"" <query.json &&
	flux jobtap remove test.so
'
test_expect_success 'job-manager: query of invalid plugin fails' '
	test_must_fail flux jobtap query foo
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
	flux start -c $(pwd)/testconf flux jobtap list > confplugins.out &&
	test_debug "cat confplugins.out" &&
	grep args confplugins.out &&
	grep test confplugins.out
'
test_expect_success 'job-manager: bad plugins config is detected' '
	mkdir -p badconf/a badconf/b badconf/c  badconf/d &&
	cat <<-EOF >badconf/a/job-manager.toml &&
	[job-manager]
	plugins = { load = "test.so" }
	EOF
	cat <<-EOF >badconf/b/job-manager.toml &&
	[[job-manager.plugins]]
	load = 42
	EOF
	cat <<-EOF >badconf/c/job-manager.toml &&
	[[job-manager.plugins]]
	remove = "notfound.so"
	EOF
	cat <<-EOF >badconf/d/job-manager.toml &&
	[[job-manager.plugins]]
	load = "notfound.so"
	EOF
	test_must_fail \
	    flux bulksubmit -n1 --watch --log=badconf.{}.log \
	        flux start -c$(pwd)/badconf/{} true ::: a b c d &&
	test_debug "echo a:; cat badconf.a.log" &&
	grep "config must be an array" badconf.a.log &&
	test_debug "echo b:; cat badconf.b.log" &&
	grep -i "expected string.*got integer" badconf.b.log &&
	test_debug "echo c:; cat badconf.c.log" &&
	grep -i "failed to find plugin to remove" badconf.c.log &&
	test_debug "echo d:; cat badconf.d.log" &&
	grep -i "no such plugin found" badconf.d.log
'
test_expect_success 'job-manager: default plugin sets priority to urgency' '
	flux jobtap remove all &&
	flux jobtap list -a | grep .priority-default &&
	jobid=$(flux submit --urgency=8 hostname) &&
	flux job wait-event -v $jobid priority &&
	test $(flux jobs -no {priority} $jobid) = 8 &&
	flux job wait-event -v $jobid clean
'
test_expect_success 'job-manager: default works with sched.prioritize' '
	ncores=$(flux resource list -s free -no {ncores}) &&
	allcores=$(flux submit -n ${ncores} sleep 1000) &&
	flux submit --cc=1-2 --flags=debug hostname >prio.jobids &&
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
	flux cancel --all &&
	flux queue drain
'
test_expect_success 'job-manager: hold plugin holds jobs' '
	flux jobtap load submit-hold.so &&
	flux bulksubmit --job-name=cc-{0} hostname ::: $(seq 1 4) \
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
	flux cancel $jobid &&
	flux job wait-event -v -t 5 $jobid clean
'
test_expect_success 'job-manager: release held jobs' '
	for name in cc-3 cc-4; do
	    jobid=$(id_byname $name) &&
	    flux job urgency $jobid 1 &&
	    flux job wait-event -t 5 -v $jobid clean
	done
'
test_expect_success 'job-manager: unload hold plugin' '
	flux jobtap remove submit-hold.so
'
test_expect_success 'job-manager: test with random priority plugin' '
	flux module reload sched-simple mode=unlimited &&
	ncores=$(flux resource list -s free -no {ncores}) &&
	sleepjob=$(flux submit -n ${ncores} sleep 3000) &&
	flux jobtap load --remove=all ${PLUGINPATH}/random.so &&
	flux bulksubmit --flags waitable --job-name=random-{} hostname \
	    ::: $(seq 1 4) &&
	flux jobs -c4 -no {name}:{priority} | sort > pri-before.out &&
	sleep 1 &&
	flux jobs -c4 -no {name}:{priority} | sort > pri-after.out &&
	test_must_fail test_cmp pri-before.out pri-after.out &&
	flux cancel $sleepjob &&
	flux job wait-event -vt 30 $sleepjob clean &&
	flux job wait --all -v
'
test_expect_success 'job-manager: run args test plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/args.so &&
	flux run hostname &&
	flux dmesg | grep args-check > args-check.log &&
	test_debug "cat args-check.log" &&
	test $(grep -c OK args-check.log) = 21
'
test_expect_success 'job-manager: run subscribe test plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/subscribe.so &&
	flux run hostname &&
	flux dmesg | grep subscribe-check > subscribe-check.log &&
	test_debug "cat subscribe-check.log" &&
	test $(grep -c OK subscribe-check.log) = 6
'
test_expect_success 'job-manager: run job_aux test plugin' '
	flux dmesg --clear &&
	flux jobtap load --remove=all ${PLUGINPATH}/job_aux.so &&
	flux run hostname
'
test_expect_success 'job-manager: job aux cleared on transition to inactive' '
	flux dmesg >aux-dmesg.out &&
	grep "test destructor invoked" aux-dmesg.out
'
test_expect_success 'job-manager: start another job and remove plugin' '
	flux dmesg --clear &&
	jobid=$(flux submit --wait-event=alloc sleep 60) &&
	flux jobtap remove job_aux.so &&
	flux cancel $jobid
'
test_expect_success 'job-manager: job aux cleared when plugin removed' '
	flux dmesg >aux-dmesg2.out &&
	grep "test destructor invoked" aux-dmesg2.out
'
test_expect_success 'job-manager: load jobtap_api test plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/jobtap_api.so &&
	id=$(flux submit sleep 1000) &&
	flux run -vvv \
		--setattr=system.lookup-id=$(flux job id $id) \
		true &&
	flux cancel $id &&
	id=$(flux submit \
		--setattr=system.expected-result=failed \
		false) &&
	test_expect_code 1 flux job attach -vEX $id &&
	test_must_fail flux job wait-event $id exception &&
	id=$(flux submit -t 0.1s \
		--setattr=system.expected-result=timeout \
		sleep 10) &&
	test_must_fail flux job wait-event -vm type=test $id exception &&
	id=$(flux submit --urgency=hold \
		--setattr=system.expected-result=canceled \
		true) &&
	flux cancel $id &&
	test_must_fail flux job wait-event -vm type=test $id exception
'
test_expect_success 'job-manager: test that job flags can be set' '
	id=$(flux submit \
	       --setattr=system.depend.set_flag=debug hostname) &&
	flux job wait-event -vt 20 $id debug.alloc-request &&
	flux job wait-event -vt 20 $id clean
'

check_event_post() {
	local state=$1
	local id

	id=$(flux submit \
	    --setattr system.${state}.post-event=testevent hostname) &&
	flux job wait-event -vt 20 $id testevent &&
	flux job wait-event -t 20 $id clean >/dev/null
}

for state in validate new depend priority run cleanup; do
    test_expect_success "job-manager: jobtap job.$state can emit events" "
        check_event_post ${state}
    "
done

test_expect_success 'job-manager: load test jobtap plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/test.so foo.test=1 &&
	flux dmesg | grep "conf={\"foo\":{\"test\":1}}"
'
test_expect_success 'job-manager: run all test plugin test modes' '
	cat <<-EOF | sort >test-modes.txt &&
	priority unset
	callback error
	sched: priority unavail
	sched: update priority
	sched: dependency-add
	sched: exception error
	annotations error
	EOF
	COUNT=$(cat test-modes.txt | wc -l) &&
	run_timeout 20 \
	  flux bulksubmit --quiet --watch \
	    --setattr=system.jobtap.test-mode={} \
	    hostname :::: test-modes.txt >test-plugin.out &&
	test_debug "cat test-plugin.out" &&
	test $COUNT = $(cat test-plugin.out | wc -l) &&
	flux jobs -ac $COUNT -no {annotations.test} | \
	    sort >test-annotations.out &&
	test_cmp test-modes.txt test-annotations.out
'
test_expect_success 'job-manager: priority type error generates nonfatal exception' '
	id=$(flux submit \
		--setattr=system.jobtap.test-mode="priority type error" \
		hostname) &&
	flux job wait-event -vm type=job.state.priority ${id} exception &&
	test "$(flux jobs -no {annotations.test} ${id})" = "priority type error" &&
	test $(flux jobs -no {state} ${id}) = "PRIORITY" &&
	flux cancel ${id} &&
	flux job wait-event -v ${id} clean
'
test_expect_success 'job-manager: jobtap plugin can raise job exception' '
	id=$(flux submit \
	     --setattr=system.jobtap.test-mode="sched: exception" \
	     hostname) &&
	flux job wait-event -v --match type=test $id exception
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
	flux bulksubmit \
	    --flags waitable --setattr=system.jobtap.test-mode={} hostname \
	    :::: test-modes.priority.get > priority.get.jobids &&
	flux python ./reprioritize.py &&
	flux queue start &&
	test_debug "flux dmesg | grep jobtap\.test" &&
	flux jobs -ac 3 -o {id}:{annotations.test}:{status} &&
	run_timeout 20 flux job wait -v --all
'
test_expect_success 'job-manager: run test plugin modes for job.validate' '
	test_expect_code 1 \
	    flux submit\
	        --setattr=system.jobtap.test-mode="validate failure" \
	        hostname >validate-failure.out 2>&1 &&
	test_debug "cat validate-failure.out" &&
	grep "rejected for testing" validate-failure.out &&
	test_expect_code 1 \
	    flux submit\
	        --setattr=system.jobtap.test-mode="validate failure nullmsg" \
	        hostname >validate-failure2.out 2>&1 &&
	test_debug "cat validate-failure2.out" &&
	grep "rejected by job-manager plugin" validate-failure2.out &&
	test_expect_code 1 \
	    flux submit\
	        --setattr=system.jobtap.test-mode="validate failure nomsg" \
	        hostname >validate-failure3.out 2>&1 &&
	test_debug "cat validate-failure3.out" &&
	grep "rejected by job-manager plugin" validate-failure3.out
'
test_expect_success 'job-manager: plugin can keep job in PRIORITY state' '
	flux jobtap load --remove=all ${PLUGINPATH}/priority-wait.so &&
	jobid=$(flux submit hostname) &&
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
	    flux bulksubmit --watch \
	        --setattr=system.jobtap.validate-test-id={} \
	        echo foo ::: 1 1 1 4 4 1 >validate-plugin.out \
	        2>validate-plugin.err &&
	test_debug "cat validate-plugin.err" &&
	grep "Job had reject_id" validate-plugin.err &&
	test 4 -eq $(grep -c foo validate-plugin.out)
'

# Ensure that the valid jobs submitted above have reached INACTIVE in the
# (eventually consistent) job-list module, then ask job-list about the invalid
# jobs.  The assumption is that the "invalidate" journal events must have have
# been processed by job-list if the "clean" event for last job in the batch,
# which is valid, has been processed.  If the "invalidate" event is not
# received, the job-list query for the invalid jobs will hang.

test_expect_success 'job-manager: get job IDs of invalid jobs' '
	grep reject_id validate-plugin.err \
	    | sed -e "s/.*jobid=//" >invalid_ids &&
	test_debug "cat invalid_ids" &&
	test 2 -eq $(wc -l <invalid_ids)
'
test_expect_success 'job-manager: get job IDs of valid jobs' '
	grep -v reject_id validate-plugin.out | grep -v foo \
	    | sed -e "s/.*jobid=//" >valid_ids &&
	test_debug "cat valid_ids" &&
	test 4 -eq $(wc -l <valid_ids)
'

# Return true if all specified jobs are shown as inactive by job-list
test_inactive() {
	flux jobs -n -o {state} $@ >test_jobs.out 2>test_jobs.err
	test $(wc -l <test_jobs.err) -eq 0 \
		-a $(grep -v INACTIVE test_jobs.out | wc -l) -eq 0
}
# Return true if all specified jobs are unknown to job-list
test_unknown() {
	run_timeout 10 \
	    flux jobs -n -o {state} $@ >test_jobs.out 2>test_jobs.err && \
	test $(wc -l <test_jobs.out) -eq 0
}

test_expect_success 'job-manager: wait for valid jobs to appear inactive' '
	while ! test_inactive $(cat valid_ids); do echo retry; sleep 0.1; done
'
test_expect_success 'job-manager: flux jobs does not list invalid jobs' '
	test_expect_code 1 test_unknown $(cat invalid_ids)
'

test_expect_success 'job-manager: plugin can manage dependencies' '
	cat <<-EOF >dep-remove.py &&
	import flux
	from flux.job import JobID
	import sys

	jobid = flux.job.JobID(sys.argv[1])
	topic = "job-manager.dependency-test.remove"
	payload = {"id": jobid, "description": sys.argv[2]}
	print(flux.Flux().rpc(topic, payload).get())
	EOF
	flux module reload job-ingest &&
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so &&
	jobid=$(flux submit --dependency=test:dependency-test hostname) &&
	flux job wait-event -vt 15 ${jobid} dependency-add &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-remove.py ${jobid} dependency-test &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'job-manager: dependency-add works from job.state.depend' '
	jobid=$(flux submit --setattr=system.dependency-test=foo true) &&
        flux job wait-event -vt 15 ${jobid} dependency-add &&
	test_debug "flux jobs -no {state} ${jobid}" &&
        test $(flux jobs -no {state} ${jobid}) = DEPEND &&
        flux python dep-remove.py ${jobid} foo &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'job-manager: job.state.depend is called on plugin load' '
	jobid=$(flux submit --dependency=test:dependency-test hostname) &&
	flux job wait-event -vt 15 ${jobid} dependency-add &&
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-remove.py ${jobid} dependency-test &&
	flux job wait-event -vt 15 ${jobid} clean
'
lineno() {
	local nline
	nline=$(grep -n $1 $2) || return 1
	echo $nline | cut -d: -f1
}
test_expect_success 'job-manager: job prolog/epilog events work' '
	flux jobtap load --remove=all ${PLUGINPATH}/perilog-test.so &&
	jobid=$(flux submit hostname) &&
	flux job attach --wait-event clean -vE $jobid 2>&1 \
	    | tee perilog-test.out &&
	n_prolog=$(lineno job.prolog-finish perilog-test.out) &&
	n_start=$(lineno job.start perilog-test.out) &&
	n_epilog=$(lineno job.epilog-finish perilog-test.out) &&
	n_free=$(lineno job.free perilog-test.out) &&
	test_debug "echo Checking that prolog-finish=$n_prolog event occurs before start=$n_start event" &&
	test_debug "echo Checking that epilog-finish=$n_epilog event occurs before free=$n_free event" &&
	test $n_prolog -lt $n_start -a $n_epilog -lt $n_free
'
test_expect_success 'job-manager: epilog works after exception during prolog' '
	flux jobtap load --remove=all ${PLUGINPATH}/perilog-test.so \
		prolog-exception=1 &&
	jobid=$(flux submit hostname) &&
	flux job attach --wait-event clean -vE $jobid 2>&1 \
	    | tee perilog-exception-test.out &&
	n_epilog=$(lineno job.epilog-finish perilog-exception-test.out) &&
	n_free=$(lineno job.free perilog-exception-test.out) &&
	test $n_epilog -lt $n_free
'
test_expect_success 'job-manager: epilog prevents clean event' '
	flux jobtap load --remove=all ${PLUGINPATH}/perilog-test.so &&
	jobid=$(flux submit --urgency=hold hostname) &&
	flux cancel $jobid &&
	flux job attach --wait-event clean -vE $jobid 2>&1 \
	    | tee perilog-epilog-clean-test.out &&
	n_epilog=$(lineno job.epilog-finish perilog-epilog-clean-test.out) &&
	n_clean=$(lineno job.clean perilog-epilog-clean-test.out) &&
	test $n_epilog -lt $n_clean
'
test_expect_success 'job-manager: job.create posts events before validation' '
	flux jobtap load --remove=all ${PLUGINPATH}/create-event.so &&
	jobid=$(flux submit hostname) &&
	flux job wait-event -v $jobid validate >create-event.out &&
	n_test=$(lineno test-event create-event.out) &&
	n_create=$(lineno validate create-event.out) &&
	test $n_test -lt $n_create
'

test_expect_success 'job-manager: job.create can reject a job' '
	flux jobtap load --remove=all ${PLUGINPATH}/create-reject.so &&
	test_must_fail flux submit hostname 2>submit.err &&
	grep nope submit.err
'
test_expect_success 'job-manager: plugins can update jobspec' '
	flux jobtap load --remove=all ${PLUGINPATH}/jobspec-update.so &&
	jobid=$(flux submit --job-name=test hostname) &&
	flux job eventlog -H $jobid &&
	flux job eventlog $jobid | grep jobspec-update &&
	flux job eventlog $jobid | grep attributes.system.update-test &&
	flux job eventlog $jobid | \
		test_must_fail grep attributes.system.run-update &&
	test_must_fail flux job wait-event --count=2 -Hv \
		--match-context=attributes.system.job.name=new \
		$jobid jobspec-update
'
test_expect_success 'job-manager: plugin can asynchronously update jobspec' '
	flux dmesg -H | grep jobspec-update &&
	jobid=$(flux submit -n1 --urgency=hold sleep 0) &&
	cat <<-EOF >update-test.py &&
	import flux
	from flux.job import JobID
	id=JobID("$jobid")
	flux.Flux().rpc(
	    "job-manager.jobspec-update.update",
	    dict(id=id, update={"attributes.system.job.name": "test"}),
	).get()
	EOF
	flux python update-test.py &&
	flux job wait-event -Hv -t 30 $jobid jobspec-update &&
	flux jobs $jobid
'
test_expect_success 'job-manager: plugin fails to load on config.update error' '
	flux jobtap remove all &&
	test_must_fail flux jobtap load ${PLUGINPATH}/config.so 2>config.err
'
test_expect_success 'job-manager: and produces reasonable error for humans' '
	grep "Error parsing" config.err
'
test_expect_success 'set up valid test configuration' '
	cat >config/test.toml <<-EOT &&
	[testconfig]
	testkey = "a string"
	EOT
	flux config reload
'
test_expect_success 'and now plugin expecting that config can load' '
	flux jobtap load ${PLUGINPATH}/config.so
'
test_expect_success 'reloading invalid configuration fails' '
	cat >config/test.toml <<-EOT &&
	[testconfig]
	testkey = 42
	EOT
	test_must_fail flux config reload 2>reload.err
'
test_expect_success 'job-manager: and produces reasonable error for humans' '
	grep "Error parsing" reload.err
'
test_expect_success 'job-manager: run a job then purge all inactives' '
	flux jobtap load --remove=all ${PLUGINPATH}/args.so &&
	flux dmesg -C &&
	flux run hostname &&
	flux job purge --force --num-limit=0 &&
	flux dmesg | grep args-check | grep OK >argsok.out
'
test_expect_success 'job-manager: job.inactive-add was called' '
	grep -q job.inactive-add argsok.out
'
test_expect_success 'job-manager: job.inactive-remove was called' '
	grep -q job.inactive-remove argsok.out
'
test_expect_success 'job-manager: test flux_jobtap_call()' '
	flux jobtap load --remove=all ${PLUGINPATH}/callee.so &&
	flux jobtap load ${PLUGINPATH}/call.so &&
	flux run hostname
'
test_expect_success 'job-manager: submit a set of jobs in various states' '
	flux submit --cc=1-$(flux resource list -no {ncores}) -n1 sleep inf &&
	flux submit --cc=1-3 -n1 sleep inf &&
	flux submit --cc=1-5 --dependency=singleton --job-name=t -n1 sleep inf
'
test_expect_success 'job-manager: test flux_jobtap_set_load_sort_order(none)' '
	flux jobtap load --remove=all ${PLUGINPATH}/load-order.so sort=none
'
test_expect_success 'job-manager: test flux_jobtap_set_load_sort_order(state)' '
	flux jobtap load --remove=all ${PLUGINPATH}/load-order.so sort=state
'
test_expect_success 'job-manager: test flux_jobtap_set_load_sort_order(-state)' '
	flux jobtap load --remove=all ${PLUGINPATH}/load-order.so sort=-state
'
test_expect_success 'job-manager: cancel all jobs' '
	flux cancel --all
'
test_done
