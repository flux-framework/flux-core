#!/bin/sh

test_description='Test cosched plugin jobspec transformation'

. "$(dirname "$0")/sharness.sh"

VALIDATOR="${SHARNESS_TEST_SRCDIR}/cli-plugins/cosched/validate_transformation.py"

test_expect_success 'normal transformation: 20 tasks across sockets' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 20 \
		--resource-type socket \
		--n-way 4 \
		--cores-per-resource 16 \
		--waste-threshold 0.3 \
		--command hostname >actual.json &&
	jq -e "
		(.resources | length) == 1 and
		.resources[0].type == \"socket\" and
		.resources[0].count == 5 and
		.resources[0].with[0].type == \"slot\" and
		.resources[0].with[0].count == 4 and
		.resources[0].with[0].with[0].type == \"core\" and
		.resources[0].with[0].with[0].count == 1 and
		.tasks[0].count.total == 20
	" actual.json
'

test_expect_success 'exact resource boundary uses one parent resource' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 5 \
		--resource-type numanode \
		--n-way 2 \
		--cores-per-resource 10 \
		--waste-threshold 0.0 >actual.json &&
	jq -e "
		.resources[0].type == \"numanode\" and
		.resources[0].count == 1 and
		.resources[0].with[0].count == 5 and
		.tasks[0].count.total == 5
	" actual.json
'

test_expect_success 'task count above boundary rounds parent count upward' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 6 \
		--resource-type numanode \
		--n-way 2 \
		--cores-per-resource 10 \
		--waste-threshold 1.0 >actual.json &&
	jq -e "
		.resources[0].type == \"numanode\" and
		.resources[0].count == 2 and
		.resources[0].with[0].count == 5 and
		.tasks[0].count.total == 6
	" actual.json
'

test_expect_success 'transformation is rejected when waste exceeds threshold' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 6 \
		--resource-type numanode \
		--n-way 2 \
		--cores-per-resource 10 \
		--waste-threshold 0.3 >actual.json &&
	jq -e "
		.resources[0].type == \"slot\" and
		.resources[0].count == 6 and
		.resources[0].with[0].type == \"core\" and
		.tasks[0].count.per_slot == 1
	" actual.json
'

test_expect_success 'transformation occurs at exact waste threshold' '
	python3 ${VALIDATOR} \
	    --allowed \
		--ntasks 8 \
		--resource-type numanode \
		--n-way 2 \
		--cores-per-resource 10 \
		--waste-threshold 0.25 >actual.json &&
	jq -e "
		.resources[0].type == \"numanode\" and
		.resources[0].count == 2 and
		.resources[0].with[0].count == 5 and
		.tasks[0].count.total == 8
	" actual.json
'

test_expect_success 'disabled plugin leaves jobspec unchanged' '
	python3 ${VALIDATOR} \
		--no-allowed \
		--ntasks 20 \
		--resource-type socket \
		--n-way 4 \
		--cores-per-resource 16 \
		--waste-threshold 1.0 >actual.json &&
	jq -e "
		.resources[0].type == \"slot\" and
		.resources[0].count == 20 and
		.resources[0].with[0].type == \"core\" and
		.resources[0].with[0].count == 1 and
		.tasks[0].count.per_slot == 1
	" actual.json
'

test_expect_success 'unset allowed is equivalent to disabled' '
	python3 ${VALIDATOR} \
		--ntasks 20 \
		--resource-type socket \
		--n-way 4 \
		--cores-per-resource 16 \
		--waste-threshold 1.0 >actual.json &&
	jq -e "
		.resources[0].type == \"slot\" and
		.resources[0].count == 20 and
		.resources[0].with[0].type == \"core\" and
		.resources[0].with[0].count == 1 and
		.tasks[0].count.per_slot == 1
	" actual.json
'

test_expect_success 'single-task jobspec is transformed correctly' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 1 \
		--resource-type numanode \
		--n-way 2 \
		--cores-per-resource 10 \
		--waste-threshold 0.0 >actual.json &&
	jq -e "
		.resources[0].type == \"numanode\" and
		.resources[0].count == 1 and
		.resources[0].with[0].count == 1 and
		.tasks[0].count.total == 1
	" actual.json
'

test_expect_success 'n-way greater than core count produces one slot per resource' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 3 \
		--resource-type socket \
		--n-way 8 \
		--cores-per-resource 4 \
		--waste-threshold 0.0 >actual.json &&
	jq -e "
		.resources[0].type == \"socket\" and
		.resources[0].count == 3 and
		.resources[0].with[0].count == 1 and
		.tasks[0].count.total == 3
	" actual.json
'

test_expect_success 'arbitrary configured resource type is preserved' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 4 \
		--resource-type ccd \
		--n-way 2 \
		--cores-per-resource 8 \
		--waste-threshold 0.0 >actual.json &&
	jq -e "
		.resources[0].type == \"ccd\" and
		.resources[0].count == 1 and
		.resources[0].with[0].count == 4 and
		.tasks[0].count.total == 4
	" actual.json
'

test_expect_success 'command is preserved during transformation' '
	python3 ${VALIDATOR} \
		--allowed \
		--ntasks 4 \
		--resource-type socket \
		--n-way 2 \
		--cores-per-resource 8 \
		--waste-threshold 0.0 \
		--command hostname --short >actual.json &&
	jq -e "
		.tasks[0].command == [\"hostname\", \"--short\"]
	" actual.json
'

test_expect_success 'zero tasks are rejected' '
	test_must_fail python3 "$VALIDATOR" \
		--ntasks 0 >actual.out 2>actual.err
'

test_expect_success 'negative task count is rejected' '
	test_must_fail python3 "$VALIDATOR" \
		--ntasks -1 >actual.out 2>actual.err
'

test_done
