#!/bin/sh
test_description='Test job validator'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J="flux python ${JOBSPEC}/y2j.py"
SUBMITBENCH="${FLUX_BUILD_DIR}/t/ingest/submitbench"
BAD_VALIDATOR=${SHARNESS_TEST_SRCDIR}/ingest/bad-validate.py
export FLUX_URI_RESOLVE_LOCAL=t

test_valid ()
{
	flux bulksubmit --quiet --wait --watch \
		sh -c "cat {} | $Y2J | $SUBMITBENCH --urgency=0 -" ::: $*
}

test_invalid ()
{
	flux bulksubmit --quiet --wait --watch \
		sh -c "cat {} | $Y2J | $SUBMITBENCH --urgency=0 -" ::: $*
	test $? -ne 0
}

# load|reload ingest modules (in proper order) with specified arguments
ingest_module ()
{
    cmd=$1; shift
    flux module ${cmd} job-ingest $* &&
    flux exec -r all -x 0 flux module ${cmd} job-ingest $*
}

test_expect_success 'flux job-validator works' '
	flux run --dry-run hostname | flux job-validator --jobspec-only
'
test_expect_success 'flux job-validator detects invalid input' '
	flux run --dry-run hostname | test_must_fail flux job-validator \
		2>invalid.error &&
	grep "Missing keys" invalid.error
'
#  Attempt to trick validator into loading a bad urllib by modification
#  of PYTHONPATH (issue #5547 reproducer). This affects all Python utils,
#  but we test the validator as a surrogate for the rest:
#
test_expect_success 'flux job-validator works with malicious PYTHONPATH' '
	mkdir -p badmod/urllib &&
	cat <<-EOF >badmod/urllib/__init__.py &&
	raise ValueError("incorrect urllib Python module loaded")
	EOF
	flux run --dry-run hostname \
	  | PYTHONPATH=$(pwd)/badmod:${PYTHONPATH} \
	       flux job-validator --jobspec-only
'
test_expect_success 'flux job-validator --list-plugins works' '
	flux job-validator --list-plugins >list-plugins.output 2>&1 &&
	test_debug "cat list-plugins.output" &&
	grep jobspec list-plugins.output &&
	grep feasibility list-plugins.output &&
	grep require-instance list-plugins.output
'
test_expect_success 'validator plugin behaves when no plugins are found' '
	cat <<-EOF >test-importer.py &&
	from flux.importer import import_plugins
	type(import_plugins("xxyyzz112233"))
	EOF
	flux python ./test-importer.py
'
test_expect_success 'validator plugin importer reports errors on import' '
	mkdir t2110plugins &&
	cat <<-EOF >t2110plugins/test.py &&
	import froufroufoxes
	EOF
	cat <<-EOF >test-importer2.py &&
	from flux.importer import import_plugins
	import_plugins("t2110plugins")
	EOF
	test_must_fail flux python ./test-importer2.py >import-fail.out 2>&1 &&
	test_debug "cat import-fail.out" &&
	grep "No module named.*froufroufoxes" import-fail.out
'
test_expect_success 'flux job-validator --help shows help for selected plugins' '
	flux job-validator --plugins=jobspec --help >help.jobspec.out 2>&1 &&
	grep require-version help.jobspec.out
'
test_expect_success 'flux job-validator errors on invalid plugin' '
	test_expect_code 1 flux job-validator --plugin=foo </dev/null &&
	test_expect_code 1 flux job-validator --plugin=/tmp </dev/null
'
test_expect_success 'flux job-validator --require-version rejects invalid arg' '
	flux run --dry-run hostname | \
		test_expect_code 1 \
		flux job-validator --jobspec-only --require-version=99 &&
	flux run --dry-run hostname | \
		test_expect_code 1 \
		flux job-validator --jobspec-only --require-version=0
'
test_expect_success 'flux job-validator rejects non-V1 jobspec' '
	flux run --dry-run hostname | jq -c ".version = 2" | \
		test_expect_code 1 \
		flux job-validator --jobspec-only --require-version=1
'
test_expect_success 'job-ingest: v1 jobspecs accepted by default' '
	test_valid ${JOBSPEC}/valid_v1/*
'
test_expect_success 'job-ingest: test jobspec validator with any version' '
	ingest_module reload \
		validator-plugins=jobspec \
		validator-args="--require-version=any"
'
test_expect_success 'job-ingest: all valid jobspecs accepted' '
	test_valid ${JOBSPEC}/valid/*
'
test_expect_success 'job-ingest: invalid jobs rejected' '
	test_invalid ${JOBSPEC}/invalid/*
'
test_expect_success 'job-ingest: stop the queue so no more jobs run' '
	flux queue stop
'
test_expect_success 'job-ingest: load feasibilty validator plugin' '
	ingest_module reload validator-plugins=feasibility
'
test_expect_success 'job-ingest: feasibility check succeeds with ENOSYS' '
	flux module remove sched-simple &&
	flux submit -g 1 hostname &&
	flux submit -n 10000 hostname &&
	flux module load sched-simple
'
test_expect_success 'job-ingest: infeasible jobs are now rejected' '
	test_must_fail flux submit -g 1 hostname 2>infeasible1.err &&
	test_debug "cat infeasible1.err" &&
	grep -i "unsupported resource type" infeasible1.err &&
	test_must_fail flux submit -n 10000 hostname 2>infeasible2.err &&
	test_debug "cat infeasible2.err" &&
	grep "unsatisfiable request" infeasible2.err &&
	test_must_fail flux submit -N 12 -n12 hostname 2>infeasible3.err &&
	test_debug "cat infeasible3.err" &&
	grep "unsatisfiable request" infeasible3.err
'
test_expect_success 'job-ingest: feasibility validator works with jobs running' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	flux queue start &&
	jobid=$(flux submit -n${ncores} sleep inf) &&
	flux job wait-event -vt 20 ${jobid} start &&
	flux queue stop &&
	flux submit -n 2 hostname &&
	test_must_fail flux submit -N 12 -n12 hostname 2>infeasible4.err &&
	grep "unsatisfiable request" infeasible4.err &&
	flux cancel ${jobid} &&
	flux job wait-event ${jobid} clean
'
test_expect_success 'job-ingest: load multiple validators' '
	ingest_module reload validator-plugins=feasibility,jobspec
'
test_expect_success 'job-ingest: jobs that fail either validator are rejected' '
	test_must_fail flux submit --setattr=.foo=bar hostname &&
	test_must_fail flux submit -n 4568 hostname
'
test_expect_success 'job-ingest: validator unexpected exit is handled' '
	ingest_module reload \
		validator-plugins=${BAD_VALIDATOR} &&
		test_must_fail flux submit hostname 2>badvalidator.out &&
	grep "unexpectedly exited" badvalidator.out
'
test_expect_success 'job-ingest: require-instance validator plugin works' '
	ingest_module reload validator-plugins=require-instance &&
	flux batch -n1 --wrap flux resource list &&
	flux submit -n1 flux start flux resource list &&
	flux submit -n1 flux broker flux resource list &&
	test_must_fail flux submit hostname &&
	test_must_fail flux submit flux getattr rank
'
test_expect_success 'job-ingest: require-instance min size can be configured' '
	ARGS="--require-instance-minnodes=2,--require-instance-mincores=4" &&
	ingest_module reload validator-plugins=require-instance \
		validator-args="$ARGS" &&
	flux submit -N1 hostname &&
	flux submit -n2 hostname &&
	test_must_fail flux submit -N4 hostname &&
	test_must_fail flux submit -n4 hostname
'
test_expect_success 'job-ingest: require-instance min size can be for nodes only' '
	ARGS="--require-instance-minnodes=2" &&
	ingest_module reload validator-plugins=require-instance \
		validator-args="$ARGS" &&
	flux submit -N1 hostname &&
	flux submit -n2 hostname &&
	test_must_fail flux submit -N4 hostname &&
	test_must_fail flux submit -n32 hostname
'
test_expect_success 'job-ingest: kill all jobs and start the queue' '
	flux cancel --all &&
	flux queue idle &&
	flux queue start
'
test_expect_success 'job-ingest: require-instance min size can use config' '
	jobid=$(flux alloc --bg -N4 \
		--conf=ingest.validator.plugins="[\"require-instance\"]" \
		--conf=ingest.validator.require-instance.minnodes=4 \
		--conf=ingest.validator.require-instance.mincores=16) &&
	flux proxy $jobid flux run hostname &&
	test_must_fail flux proxy $jobid flux run -N4 hostname 2>config.err &&
	grep "Direct job submission disabled for jobs >= 4" config.err
'
test_done
