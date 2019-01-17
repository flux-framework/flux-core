#!/bin/sh

test_description='Test the jobspec emission tool'

. `dirname $0`/sharness.sh

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
VALIDATE=${JOBSPEC}/validate.py
SCHEMA=${JOBSPEC}/schema.json
MINI_SCHEMA=${JOBSPEC}/minimal-schema.json
EMIT=${JOBSPEC}/emit-jobspec.py

validate_emission() {
    ${EMIT} $@ | ${VALIDATE} --schema ${SCHEMA}
}

validate_minimal_emission() {
    ${EMIT} $@ | ${VALIDATE} --schema ${MINI_SCHEMA}
}

test_expect_success 'emit-jobspec.py with no args emits valid jobspec' '
    validate_emission sleep 1
'

test_expect_success 'emit-jobspec.py with just num_tasks emits valid jobspec' '
    validate_emission -n4 sleep 1
'

test_expect_success 'emit-jobspec.py with just cores_per_task emits valid jobspec' '
    validate_emission -c4 sleep 1
'

test_expect_success 'emit-jobspec.py without num_nodes emits valid jobspec' '
    validate_emission -n4 -c1 sleep 1
'

test_expect_success 'emit-jobspec.py with all args emits valid jobspec' '
    validate_emission -N4 -n4 -c1 sleep 1
'

test_expect_success 'emit-jobspec.py with pretty printing emits valid jobspec' '
    validate_emission --pretty sleep 1
'

test_expect_success 'emit-jobspec.py with no args emits minimal jobspec' '
    validate_minimal_emission sleep 1
'

test_expect_success 'emit-jobspec.py with just num_tasks emits minimal jobspec' '
    validate_minimal_emission -n4 sleep 1
'

test_expect_success 'emit-jobspec.py with just cores_per_task emits minimal jobspec' '
    validate_minimal_emission -c4 sleep 1
'

test_expect_success 'emit-jobspec.py without num_nodes emits minimal jobspec' '
    validate_minimal_emission -n4 -c1 sleep 1
'

test_expect_success 'emit-jobspec.py with all args emits minimal jobspec' '
    validate_minimal_emission -N4 -n4 -c1 sleep 1
'
test_done
