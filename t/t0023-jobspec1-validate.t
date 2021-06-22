#!/bin/sh

test_description='Test the libjob jobspec1 validation'

. `dirname $0`/sharness.sh

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J="flux python ${JOBSPEC}/y2j.py"
VALIDATE1=${FLUX_BUILD_DIR}/t/util/jobspec1-validate

# Emit a "prereq" string if test it to be skipped.
skip() {
	case $1 in
		*invalid_yaml1.yaml)
			echo NATIVE_YAML_PARSE
			;;
		*resource_count_missing_*.yaml)
			echo RESOURCE_COUNT_OBJECT
			;;
	esac
}

validate_v1() {
	$Y2J <$1 | ${VALIDATE1}
}

for jobspec in ${JOBSPEC}/valid_v1/*.yaml; do
	testname=jobspec_v1/$(basename $jobspec)
	test_expect_success $(skip $testname) \
		'valid_v1: '$testname "validate_v1 $jobspec"
done

for jobspec in ${JOBSPEC}/invalid/*.yaml; do
	testname=`basename $jobspec`
	test_expect_success $(skip $testname) \
		'invalid: '$testname "test_must_fail validate_v1 $jobspec"
done

for jobspec in ${JOBSPEC}/invalid_v1/*.yaml; do
	testname=`basename $jobspec`
	test_expect_success $(skip $testname) \
		'invalid_v1: '$testname "test_must_fail validate_v1 $jobspec"
done

test_done
