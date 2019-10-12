#!/bin/sh

test_description='Test the jobspec schema validation'

. `dirname $0`/sharness.sh

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
VALIDATE="flux python ${JOBSPEC}/validate.py"
SCHEMA=${FLUX_SOURCE_DIR}/src/modules/job-ingest/schemas/jobspec.jsonschema
SCHEMA_V1=${FLUX_SOURCE_DIR}/src/modules/job-ingest/schemas/jobspec_v1.jsonschema

validate() {
   ${VALIDATE} --schema ${SCHEMA} $1
}
validate_v1() {
   ${VALIDATE} --schema ${SCHEMA_V1} $1
}

# Check that the valid jobspecs all pass
for jobspec in ${JOBSPEC}/valid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success 'valid: '$testname "validate $jobspec"
done
# V1 validates against general
for jobspec in ${JOBSPEC}/valid_v1/*.yaml; do
    testname=jobspec_v1/$(basename $jobspec)
    test_expect_success 'valid: '$testname "validate $jobspec"
done
# V1 validates against V1
for jobspec in ${JOBSPEC}/valid_v1/*.yaml; do
    testname=jobspec_v1/$(basename $jobspec)
    test_expect_success 'valid_v1: '$testname "validate_v1 $jobspec"
done

# Check that the invalid jobspec all fail
for jobspec in ${JOBSPEC}/invalid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success 'error: '$testname "test_must_fail validate $jobspec"
done

test_done
