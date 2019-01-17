#!/bin/sh

test_description='Test the jobspec schema validation'

. `dirname $0`/sharness.sh

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
VALIDATE=${JOBSPEC}/validate.py
SCHEMA=${FLUX_SOURCE_DIR}/src/modules/job-ingest/schemas/jobspec.jsonschema

validate() {
   ${VALIDATE} --schema ${SCHEMA} $1
}

# Check that the valid jobspecs all pass
for jobspec in ${JOBSPEC}/valid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success 'valid: '$testname "validate $jobspec"
done

# Check that the invalid jobspec all fail
for jobspec in ${JOBSPEC}/invalid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success 'error: '$testname "test_must_fail validate $jobspec"
done

test_done
