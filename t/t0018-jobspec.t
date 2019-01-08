#!/bin/sh

test_description='Test the jobspec parsing library'

. `dirname $0`/sharness.sh

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
validate="${FLUX_BUILD_DIR}/src/common/libjobspec/test_validate"

# Check that the valid jobspecs all pass
for jobspec in ${JOBSPEC}/valid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success 'valid: '$testname "$validate $jobspec"
done

# Check that the invalid jobspec all fail
for jobspec in ${JOBSPEC}/invalid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success 'error: '$testname "test_must_fail $validate $jobspec"
done

test_done
