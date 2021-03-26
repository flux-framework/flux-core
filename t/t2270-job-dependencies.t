#!/bin/sh

test_description='Test job dependencies'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

test_expect_success 'create dep-remove.py' '
	cat <<-EOF >dep-remove.py
	import flux
	from flux.job import JobID
	import sys

	jobid = flux.job.JobID(sys.argv[1])
	name = sys.argv[2]
	topic = "job-manager.dependency-test.remove"
	print(flux.Flux().rpc(topic, {"id": jobid, "description": name}).get())
	EOF
'
test_expect_success 'job-manager: load dependency-test plugin' '
	flux jobtap load ${PLUGINPATH}/dependency-test.so
'
test_expect_success 'job-manager: dependency-test plugin is working' '
	jobid=$(flux mini submit hostname) &&
	flux job wait-event -t 15 -m description=dependency-test \
		${jobid} dependency-add &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-remove.py ${jobid} dependency-test &&
	flux job wait-event -t 15 -m description=dependency-test \
		${jobid} dependency-remove &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'job dependencies are available in listing tools' '
	jobid=$(flux mini submit \
		--setattr=system.dependencies="[\"foo\"]" \
		hostname) &&
	flux job wait-event -t 15 -m description=foo ${jobid} dependency-add &&
	flux jobs -o {id}:{dependencies} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "foo" &&
	flux job list | grep "dependencies.*foo" &&
	${FLUX_BUILD_DIR}/t/job-manager/list-jobs | grep "dependencies.*foo" &&
	flux python dep-remove.py ${jobid} foo &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'multiple job dependencies works' '
	jobid=$(flux mini submit \
		--setattr=system.dependencies="[\"foo\",\"bar\"]" \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	flux jobs -o {id}:{dependencies} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "foo,bar" &&
	flux python dep-remove.py ${jobid} bar &&
	flux jobs -o {id}:{dependencies} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "foo" &&
	flux python dep-remove.py ${jobid} foo &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'multiple dependency-add with same description is ignored' '
	jobid=$(flux mini submit \
		--setattr=system.dependencies="[\"bar\",\"bar\"]" \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	flux job eventlog ${jobid} &&
	flux jobs -o {id}:{dependencies} ${jobid} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "bar" &&
	flux python dep-remove.py ${jobid} bar &&
	flux job wait-event -vt 15 ${jobid} clean
'

test_expect_success 'invalid dependency-remove is ignored' '
	jobid=$(flux mini submit \
		--setattr=system.dependencies="[\"bar\"]" \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	test_must_fail flux python dep-remove.py ${jobid} foo  &&
	flux job eventlog ${jobid} &&
	flux jobs -o {id}:{dependencies} ${jobid} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "bar" &&
	flux python dep-remove.py ${jobid} bar &&
	flux job wait-event -vt 15 ${jobid} clean &&
	test "$(flux jobs -no {dependencies:h} ${jobid})" = "-"
'
test_expect_success 'dependencies are re-established after restart' '
	jobid=$(flux mini submit \
		--setattr=system.dependencies="[\"foo\",\"bar\"]" \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	flux jobs -o {id}:{dependencies} &&
	flux python dep-remove.py ${jobid} bar &&
	flux jobs -o {id}:{dependencies} &&
	flux module remove job-list &&
	flux module reload job-manager &&
	flux jobtap load ${PLUGINPATH}/dependency-test.so &&
	flux module load job-list &&
	flux module reload -f sched-simple &&
	flux module reload -f job-exec &&
	flux job eventlog ${jobid} &&
	flux jobs -o {id}:{dependencies} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "foo" &&
	flux python dep-remove.py ${jobid} foo &&
	flux jobs -o {id}:{dependencies} &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_done
