#!/bin/sh

test_description='Test the jobspec parsing library'

. `dirname $0`/sharness.sh

validate="${FLUX_BUILD_DIR}/src/cmd/flux-jobspec-validate"

# Check that the valid jobspecs all pass
for jobspec in ${FLUX_SOURCE_DIR}/t/jobspec/valid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success $testname "$validate $jobspec"
done

# Check that the invalid jobspec all fail
for jobspec in ${FLUX_SOURCE_DIR}/t/jobspec/invalid/*.yaml; do
    testname=`basename $jobspec`
    test_expect_success $testname "test_must_fail $validate $jobspec"
done

test_done
