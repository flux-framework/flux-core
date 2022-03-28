#!/bin/sh
test_description='Test job validator'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J="flux python ${JOBSPEC}/y2j.py"
SUBMITBENCH="${FLUX_BUILD_DIR}/t/ingest/submitbench"
SCHEMA=${FLUX_SOURCE_DIR}/src/modules/job-ingest/schemas/jobspec.jsonschema
BAD_VALIDATOR=${SHARNESS_TEST_SRCDIR}/ingest/bad-validate.py

test_valid ()
{
	flux mini bulksubmit --quiet --wait --watch \
		sh -c "cat {} | $Y2J | $SUBMITBENCH --urgency=0 -" ::: $*
}

test_invalid ()
{
	flux mini bulksubmit --quiet --wait --watch \
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
	flux mini run --dry-run hostname | flux job-validator --jobspec-only
'
test_expect_success 'flux job-validator --list-plugins works' '
	flux job-validator --list-plugins >list-plugins.output 2>&1 &&
	test_debug "cat list-plugins.output" &&
	grep jobspec list-plugins.output &&
	grep feasibility list-plugins.output &&
	grep require-instance list-plugins.output
'
test_expect_success 'flux job-validator --help shows help for selected plugins' '
	flux job-validator --plugins=jobspec --help >help.jobspec.out 2>&1 &&
	flux job-validator --plugins=schema --help >help.schema.out 2>&1 &&
	flux job-validator --plugins=feasibility --help >help.feas.out 2>&1 &&
	grep require-version help.jobspec.out &&
	grep schema=SCHEMA help.schema.out &&
	grep feasibility-service=NAME help.feas.out
'
test_expect_success 'flux job-validator errors on invalid plugin' '
	test_expect_code 1 flux job-validator --plugin=foo </dev/null &&
	test_expect_code 1 flux job-validator --plugin=/tmp </dev/null
'
test_expect_success 'flux job-validator --require-version rejects invalid arg' '
	flux mini run --dry-run hostname | \
		test_expect_code 1 \
		flux job-validator --jobspec-only --require-version=99 &&
	flux mini run --dry-run hostname | \
		test_expect_code 1 \
		flux job-validator --jobspec-only --require-version=0
'
test_expect_success HAVE_JQ 'flux job-validator rejects non-V1 jobspec' '
	flux mini run --dry-run hostname | jq -c ".version = 2" | \
		test_expect_code 1 \
		flux job-validator --jobspec-only --require-version=1
'
test_expect_success 'flux job-validator --schema rejects invalid arg' '
	flux mini run --dry-run hostname | \
		test_expect_code 1 \
		flux job-validator --jobspec-only \
			--plugins=schema \
			--schema=noexist
'
test_expect_success HAVE_JQ 'flux job-validator --feasibility-service works ' '
	flux mini run -n 4888 --dry-run hostname | \
		flux job-validator --jobspec-only --plugins=feasibility \
		| jq -e ".errnum != 0" &&
	flux mini run -n 4888 --dry-run hostname | \
		flux job-validator --jobspec-only \
			--plugins=feasibility \
			--feasibility-service=kvs.ping \
		| jq -e ".errnum == 0"
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
test_expect_success 'job-ingest: test python jsonschema validator' '
	ingest_module reload \
		validator-plugins=schema \
		validator-args=--schema,${SCHEMA}
'
test_expect_success 'job-ingest: YAML jobspec is rejected by schema validator' '
	test_must_fail flux job submit --urgency=0 ${JOBSPEC}/valid/basic.yaml
'
test_expect_success 'job-ingest: valid jobspecs accepted by schema validator' '
	test_valid ${JOBSPEC}/valid/*
'
test_expect_success 'job-ingest: invalid jobs rejected by schema validator' '
	test_invalid ${JOBSPEC}/invalid/*
'
test_expect_success 'job-ingest: stop the queue so no more jobs run' '
	flux queue stop
'
test_expect_success 'job-ingest: load feasibilty validator plugin' '
	ingest_module reload validator-plugins=feasibility
'
test_expect_success 'job-ingest: feasibility check succceeds with ENOSYS' '
	flux module remove sched-simple &&
	flux mini submit -g 1 hostname &&
	flux mini submit -n 10000 hostname &&
	flux module load sched-simple
'
test_expect_success 'job-ingest: infeasible jobs are now rejected' '
	test_must_fail flux mini submit -g 1 hostname 2>infeasible1.err &&
	test_debug "cat infeasible1.err" &&
	grep -i "unsupported resource type" infeasible1.err &&
	test_must_fail flux mini submit -n 10000 hostname 2>infeasible2.err &&
	test_debug "cat infeasible2.err" &&
	grep "unsatisfiable request" infeasible2.err &&
	test_must_fail flux mini submit -N 12 -n12 hostname 2>infeasible3.err &&
	test_debug "cat infeasible3.err" &&
	grep "unsatisfiable request" infeasible3.err
'
test_expect_success 'job-ingest: feasibility validator works with jobs running' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	flux queue start &&
	jobid=$(flux mini submit -n${ncores} sleep inf) &&
	flux job wait-event -vt 20 ${jobid} start &&
	flux queue stop &&
	flux mini submit -n 2 hostname &&
	test_must_fail flux mini submit -N 12 -n12 hostname 2>infeasible4.err &&
	grep "unsatisfiable request" infeasible4.err &&
	flux job cancel ${jobid} &&
	flux job wait-event ${jobid} clean
'
test_expect_success 'job-ingest: load multiple validators' '
	ingest_module reload validator-plugins=feasibility,jobspec
'
test_expect_success 'job-ingest: jobs that fail either validator are rejected' '
	test_must_fail flux mini submit --setattr=foo=bar hostname &&
	test_must_fail flux mini submit -n 4568 hostname
'
test_expect_success 'job-ingest: validator unexpected exit is handled' '
	ingest_module reload \
		validator-plugins=${BAD_VALIDATOR} &&
		test_must_fail flux mini submit hostname 2>badvalidator.out &&
	grep "unexpectedly exited" badvalidator.out
'
test_expect_success 'job-ingest: require-instance validator plugin works' '
	ingest_module reload validator-plugins=require-instance &&
	flux mini batch -n1 --wrap flux resource list &&
	flux mini submit -n1 flux start flux resource list &&
	flux mini submit -n1 flux broker flux resource list &&
	test_must_fail flux mini submit hostname &&
	test_must_fail flux mini submit flux getattr rank
'
test_done
