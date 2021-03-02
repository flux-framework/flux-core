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
    local rc=0
    for job in $*; do
        cat ${job} | ${Y2J} | ${SUBMITBENCH} - || rc=1
    done
    return ${rc}
}

test_invalid ()
{
    local rc=0
    for job in $*; do
        cat ${job} | ${Y2J} | ${SUBMITBENCH} - && rc=1
    done
    return ${rc}
}

# load|reload ingest modules (in proper order) with specified arguments
ingest_module ()
{
    cmd=$1; shift
    flux module ${cmd} job-ingest $* &&
    flux exec -r all -x 0 flux module ${cmd} job-ingest $*
}

test_expect_success 'stop job queue' '
	flux queue stop
'
test_expect_success 'job-ingest: valid jobspecs accepted' '
	test_valid ${JOBSPEC}/valid/*
'
test_expect_success 'job-ingest: invalid jobs rejected' '
	test_invalid ${JOBSPEC}/invalid/*
'
test_expect_success 'job-ingest: test jobspec validator with version 1' '
	ingest_module reload \
		validator-plugins=jobspec \
		validator-args="--require-version,1"
'
test_expect_success 'job-ingest: v1 jobspecs accepted with v1 requirement' '
	test_valid ${JOBSPEC}/valid_v1/*
'
test_expect_success 'job-ingest: test python jsonschema validator' '
	ingest_module reload \
		validator-plugins=schema \
		validator-args=--schema,${SCHEMA}
'
test_expect_success 'job-ingest: YAML jobspec is rejected by schema validator' '
	test_must_fail flux job submit ${JOBSPEC}/valid/basic.yaml
'
test_expect_success 'job-ingest: valid jobspecs accepted by schema validator' '
	test_valid ${JOBSPEC}/valid/*
'
test_expect_success 'job-ingest: invalid jobs rejected by schema validator' '
	test_invalid ${JOBSPEC}/invalid/*
'
test_expect_success 'job-ingest: validator unexpected exit is handled' '
	ingest_module reload \
		validator-plugins=${BAD_VALIDATOR} &&
		test_must_fail flux mini submit hostname 2>badvalidator.out &&
	grep "unexpectedly exited" badvalidator.out
'
test_done
