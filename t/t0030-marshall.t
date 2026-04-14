#!/bin/sh

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile

test_description='Test message marshalling across versions'

. `dirname $0`/sharness.sh

marshall=${FLUX_BUILD_DIR}/t/util/marshall
inputs=${SHARNESS_TEST_SRCDIR}/marshall

test_expect_success 'generate encoded message output for this version' '
	${marshall} encode >this &&
	${marshall} decode <this
'

test_expect_success 'output from this version matches known valid version' '
	cmp ${inputs}/valid/v0.47.data this
'

for data in ${inputs}/valid/*.data; do
    testname=`basename $data`
    test_expect_success 'valid: '$testname "${marshall} decode <$data"
done
for data in ${inputs}/invalid/*.data; do
    testname=`basename $data`
    test_expect_failure 'invalid: '$testname "${marshall} decode <$data"
done


test_done
