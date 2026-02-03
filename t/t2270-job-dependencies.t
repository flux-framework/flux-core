#!/bin/sh

test_description='Test job dependencies'

. $(dirname $0)/sharness.sh

# Setup a config dir for this test_under_flux instance so we can
#  configure jobtap plugins on reload
export FLUX_CONF_DIR=$(pwd)/conf.d
mkdir -p conf.d

test_under_flux 4 job -Slog-stderr-level=1

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs


test_expect_success 'flux run: --dependency option works' '
	flux run --dry-run \
		--env=-* \
		--dependency=foo:1234 \
		--dependency=foo:3.1415 \
		--dependency=foo:f1?val=bar \
		--dependency="foo:f1?val=a&val=b" true | \
		jq '.attributes.system.dependencies' > deps.json &&
	test_debug "cat deps.json" &&
	jq -e ".[0].scheme == \"foo\"" < deps.json &&
	jq -e ".[0].value == \"1234\"" < deps.json &&
	jq -e ".[1].value == \"3.1415\"" < deps.json &&
	jq -e ".[2].scheme == \"foo\"" < deps.json &&
	jq -e ".[2].value == \"f1\""   < deps.json &&
	jq -e ".[2].val == \"bar\""    < deps.json &&
	jq -e ".[3].val[0] == \"a\""   < deps.json &&
	jq -e ".[3].val[1] == \"b\""   < deps.json
'
test_expect_success 'submitted job with unknown dependency scheme is rejected' '
	test_must_fail flux submit --dependency=invalid:value hostname
'
test_expect_success 'job with too long dependency scheme is rejected' '
	test_must_fail flux submit \
		--dependency=$(python -c "print \"x\"*156"):value hostname
'
test_expect_success 'reload ingest with disabled validator' '
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'submitted job with invalid dependencies is rejected' '
	test_must_fail flux submit \
		--setattr=system.dependencies={} \
		hostname > not-an-array.out 2>&1 &&
	test_debug "cat not-an-array.out" &&
	grep -q "must be an array" not-an-array.out &&
	test_must_fail flux submit \
		--setattr=system.dependencies="[{\"foo\":1}]" \
		hostname > empty-object.out 2>&1 &&
	test_debug "cat empty-object.out" &&
	grep -q "missing" empty-object.out
'
test_expect_success 'reload ingest with default validator' '
	flux module reload -f job-ingest
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
test_expect_success 'create dep-check.py' '
	cat <<-EOF >dep-check.py
	import flux
	from flux.job import JobID
	import sys

	jobid = flux.job.JobID(sys.argv[1])
	name = sys.argv[2]
	topic = "job-manager.dependency-test.check"
	flux.Flux().rpc(topic, {"id": jobid, "name": name}).get()
	print(f"dependency-test plugin has {name} cached for {jobid}")
	EOF
'
test_expect_success 'job-manager: load dependency-test plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so
'
test_expect_success 'job-manager: dependency-test plugin is working' '
	jobid=$(flux submit --dependency=test:deptest true) &&
	flux job wait-event -t 15 -m description=deptest \
		${jobid} dependency-add &&
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux python dep-check.py ${jobid} deptest &&
	flux python dep-remove.py ${jobid} deptest &&
	flux job wait-event -t 15 -m description=deptest \
		${jobid} dependency-remove &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'plugin rejects job with malformed dependency spec' '
	test_must_fail flux submit \
		--dependency=test:failure?remove=bad \
		hostname >baddeps.out 2>&1 &&
	test_debug "cat baddeps.out" &&
	grep "failed to unpack dependency" baddeps.out
'
test_expect_success 'job dependencies are available in listing tools' '
	jobid=$(flux submit \
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
	jobid=$(flux submit \
		--dependency=test:foo \
		--dependency=test:bar \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add &&
	flux jobs -o {id}:{dependencies} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "foo,bar" &&
	flux python dep-check.py ${jobid} foo &&
	flux python dep-check.py ${jobid} bar &&
	flux python dep-remove.py ${jobid} bar &&
	test_must_fail flux python dep-check.py ${jobid} bar &&
	flux jobs -o {id}:{dependencies} &&
	test "$(flux jobs -no {dependencies} ${jobid})" = "foo" &&
	flux python dep-remove.py ${jobid} foo &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'multiple dependency-add with same description is ignored' '
	jobid=$(flux submit \
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
	id=$(flux submit \
		--dependency=test:bar?remove=1 \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${id} dependency-add &&
	flux job wait-event -t 15 -m description=bar ${id} dependency-remove &&
	flux job wait-event -vt 15 ${id} clean
'
test_expect_success 'dependency add/remove in callback does not incorrectly release job' '
	id=$(flux submit \
		--dependency=test:bar?remove=1 \
		--dependency=test:foo \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${id} dependency-add &&
	flux job wait-event -t 15 -m description=bar ${id} dependency-remove &&
	test "$(flux jobs -no {dependencies} ${id})" = "foo" &&
	test "$(flux jobs -no {state} ${id})" = "DEPEND" &&
	flux python dep-check.py ${id} foo &&
	flux python dep-remove.py ${id} foo &&
	flux job wait-event -vt 15 ${id} clean
'
test_expect_success 'restart: start job with 2 dependencies to test restart' '
	jobid=$(flux submit \
		--dependency=test:foo \
		--dependency=test:bar \
		hostname) &&
	flux job wait-event -t 15 -m description=bar ${jobid} dependency-add
'
test_expect_success 'restart: remove 1 of 2 dependencies' '
	flux jobs -o {id}:{dependencies} &&
	flux python dep-remove.py ${jobid} bar &&
	flux jobs -o {id}:{dependencies} &&
	flux python dep-check.py ${jobid} foo
'
job_manager_restart() {
	flux module remove job-list &&
	flux module reload job-manager &&
	flux module load job-list &&
	flux module reload -f sched-simple &&
	flux module reload -f job-exec
}
test_expect_success 'restart: reload job-manager' '
	job_manager_restart
'
test_expect_success 'restart: job dependency preserved' '
	test $(flux jobs -no {state} ${jobid}) = DEPEND &&
	flux jobs -o {id}:{dependencies} &&
	test $(flux jobs -no {dependencies} ${jobid}) = "foo"
'
test_expect_success 'restart: job non-fatal exception due to missing plugin' '
	flux job wait-event -t 1 -m type=dependency ${jobid} exception
'
test_expect_success 'restart: reload dependency test plugin' '
	flux jobtap load --remove=all ${PLUGINPATH}/dependency-test.so
'
test_expect_success 'restart: job.dependency.test called for jobs on load' '
	flux python dep-check.py ${jobid} foo
'
test_expect_success 'restart: subsequent removal of dependency releases job' '
	flux python dep-remove.py ${jobid} foo &&
	flux jobs -o {id}:{dependencies} &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_expect_success 'restart: add plugin to instance config' '
	mkdir -p conf &&
	cat <<-EOF >conf.d/test.toml &&
	[[job-manager.plugins]]
	load = "${PLUGINPATH}/dependency-test.so"
	EOF
	flux config reload
'
test_expect_success 'restart: restart calls job.dependency.* callbacks' '
	jobid=$(flux submit --dependency=test:foo hostname) &&
	flux job wait-event ${jobid} dependency-add &&
	job_manager_restart &&
	flux jobtap list | grep dependency &&
	test $(flux jobs -no {dependencies} ${jobid}) = "foo" &&
	flux python dep-check.py ${jobid} foo &&
	flux python dep-remove.py ${jobid} foo &&
	flux job wait-event -vt 15 ${jobid} clean
'
test_done
