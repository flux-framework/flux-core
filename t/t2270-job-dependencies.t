#!/bin/sh

test_description='Test job dependencies'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

test_expect_success HAVE_JQ 'flux-mini: --dependency option works' '
	flux mini run --dry-run \
		--env=-* \
		--dependency=foo:1234 \
		--dependency=foo:f1?val=bar \
		--dependency="foo:f1?val=a&val=b" true | \
		jq '.attributes.system.dependencies' > deps.json &&
	test_debug "cat deps.json" &&
	jq -e ".[0].scheme == \"foo\"" < deps.json &&
	jq -e ".[0].value == 1234" < deps.json &&
	jq -e ".[1].scheme == \"foo\"" < deps.json &&
	jq -e ".[1].value == \"f1\""   < deps.json &&
	jq -e ".[1].val == \"bar\""    < deps.json &&
	jq -e ".[2].val[0] == \"a\""   < deps.json &&
	jq -e ".[2].val[1] == \"b\""   < deps.json
'
test_expect_success 'submitted job with unknown dependency scheme is rejected' '
	test_must_fail flux mini submit --dependency=invalid:value hostname
'
test_expect_success 'job with too long dependency scheme is rejected' '
	test_must_fail flux mini submit \
		--dependency=$(python -c "print \"x\"*156"):value hostname
'
test_expect_success 'submitted job with invalid dependencies is rejected' '
	test_must_fail flux mini submit \
		--setattr=system.dependencies={} \
		hostname > not-an-array.out 2>&1 &&
	test_debug "cat not-an-array.out" &&
	grep -q "must be an array" not-an-array.out &&
	test_must_fail flux mini submit \
		--setattr=system.dependencies="[{\"foo\":1}]" \
		hostname > empty-object.out 2>&1 &&
	test_debug "cat empty-object.out" &&
	grep -q "missing" empty-object.out
'
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
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so
'
test_expect_success 'job-manager: dependency-test plugin is working' '
	jobid=$(flux mini submit --dependency=test:deptest true) &&
	flux job wait-event -t 15 -m description=deptest \
		${jobid} dependency-add &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-remove.py ${jobid} deptest &&
	flux job wait-event -t 15 -m description=deptest \
		${jobid} dependency-remove &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'plugin rejects job with malformed dependency spec' '
	test_must_fail flux mini submit \
		--dependency=test:failure?remove=bad \
		hostname >baddeps.out 2>&1 &&
	test_debug "cat baddeps.out" &&
	grep "failed to unpack dependency" baddeps.out
'
test_expect_success 'job dependencies are available in listing tools' '
	jobid=$(flux mini submit \
		--dependency=test:foo \
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
		--dependency=test:foo \
		--dependency=test:bar \
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
		--dependency=test:bar \
		--dependency=test:bar \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	flux job eventlog ${jobid} &&
	flux jobs -o {id}:{dependencies} ${jobid} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "bar" &&
	flux python dep-remove.py ${jobid} bar &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'dependency can be removed in job.dependency callback' '
	id=$(flux mini submit \
		--dependency=test:bar?remove=1 \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${id} dependency-add &&
	flux job wait-event -t 15 -m description=bar ${id} dependency-remove &&
	flux job wait-event -vt 15 ${id} clean
'
test_expect_success 'invalid dependency-remove is ignored' '
	jobid=$(flux mini submit \
		--dependency=test:bar \
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
		--dependency=test:foo \
		--dependency=test:bar \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	flux jobs -o {id}:{dependencies} &&
	flux python dep-remove.py ${jobid} bar &&
	flux jobs -o {id}:{dependencies} &&
	flux module remove job-list &&
	flux module reload job-manager &&
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so &&
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
