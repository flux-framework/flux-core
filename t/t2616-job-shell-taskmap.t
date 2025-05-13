#!/bin/sh
#
test_description='Test flux-submit/flux-shell taskmap plugin support'

. `dirname $0`/sharness.sh

test_under_flux 4 job

# Test that actual task ranks match expected ranks.
# Assumes job output is `echo $FLUX_TASK_RANK: $(flux getattr rank)`
test_check_taskmap() {
	local id=$1
	flux job attach $id | sort -n >$id.output &&
	flux job taskmap --to=multiline $id >$id.expected &&
	test_cmp $id.expected $id.output
}

test_expect_success 'create script for testing task mapping' '
	cat <<-EOF >map.sh &&
	#!/bin/sh
	echo \$FLUX_TASK_RANK: \$(flux getattr rank)
	EOF
	chmod +x map.sh
'
test_expect_success 'flux job taskmap works' '
	id=$(flux submit -N4 --tasks-per-node=4 ./map.sh) &&
	test_must_fail flux job taskmap &&
	test "$(flux job taskmap $id)" = "[[0,4,4,1]]" &&
	test "$(flux job taskmap --taskids=0 $id)" = "0-3" &&
	test "$(flux job taskmap --ntasks=0 $id)" = "4" &&
	test "$(flux job taskmap --nodeid=15 $id)" = "3" &&
	test "$(flux job taskmap --hostname=0 $id)" = "$(hostname)" &&
	test "$(flux job taskmap --to=pmi $id)" = "(vector,(0,4,4))" &&
	flux job taskmap --to=hosts $id > taskmap.hosts &&
	cat <<-EOF >taskmap.hosts.expected &&
	$(hostname): 0-3
	$(hostname): 4-7
	$(hostname): 8-11
	$(hostname): 12-15
	EOF
	test_cmp taskmap.hosts.expected taskmap.hosts
'
test_expect_success 'flux job taskmap works with taskmap on cmdline' '
	flux job taskmap --to=raw "[[0,4,4,1]]" &&
	flux job taskmap --to=raw "[[0,4,1,4]]"
'
test_expect_success 'flux job taskmap fails with invalid taskmap on cmdline' '
	test_must_fail flux job taskmap "[[0,4,4]]"
'
test_expect_success 'flux job taskmap fails for canceled job' '
	flux queue stop &&
	id=$(flux submit -N4 true) &&
	flux cancel $id &&
	flux queue start &&
	test_must_fail flux job taskmap $id
'
test_expect_success 'flux job taskmap fails for invalid job' '
	test_must_fail flux job taskmap f1
'
test_expect_success 'flux job taskmap fails with invalid arguments' '
	id=$(flux submit -N4 --tasks-per-node=4 ./map.sh) &&
	flux job taskmap $id &&
	test_must_fail flux job taskmap --taskids=4 $id 2>no-taskids.err &&
	test_debug "cat no-taskids.err" &&
	grep "No taskids for node 4" no-taskids.err &&
	test_must_fail flux job taskmap --ntasks=4 $id  2>no-ntasks.err &&
	test_debug "cat no-ntasks.err" &&
	grep "failed to get task count for node 4" no-ntasks.err &&
	test_must_fail flux job taskmap --nodeid=24 $id 2>no-nodeid.err &&
	test_debug "cat no-nodeid.err" &&
	grep "failed to get nodeid for task 24" no-nodeid.err &&
	test_must_fail flux job taskmap --to=foo $id 2>bad-to.err &&
	test_debug "cat bad-to.err" &&
	grep "invalid value" bad-to.err &&
	test_must_fail flux job taskmap --hostname=0 "[[0,1,1,1]]" 2>no-hosts.err &&
	grep "without a jobid" no-hosts.err &&
	test_must_fail flux job taskmap --to=hosts "[[0,1,1,1]]" 2>no-hosts2.err &&
	grep "without a jobid" no-hosts2.err
'
test_expect_success 'taskmap is posted in shell.start event' '
	id=$(flux submit ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,1,1,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap is correct for --tasks-per-node=4' '
	id=$(flux submit -N4 --tasks-per-node=4 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,4,4,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap is unchanged with --taskmap=block' '
	id=$(flux submit -N4 --tasks-per-node=4 \
		--taskmap=block ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,4,4,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'shell dies with --taskmap=block:oops' '
	test_must_fail_or_be_terminated flux run -N4 --tasks-per-node=4 \
		--taskmap=block:oops ./map.sh
'
test_expect_success 'taskmap is correct for --tasks-per-node=2' '
	id=$(flux submit -N4 --tasks-per-node=2 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,4,2,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=cyclic works with valid args' '
	id=$(flux submit --taskmap=cyclic -N4 --tasks-per-node=4 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,4,1,4]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=cyclic:2 works' '
	id=$(flux submit --taskmap=cyclic:2 -N4 --tasks-per-node=4 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,4,2,2]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=cyclic:3 works' '
	id=$(flux submit --taskmap=cyclic:3 -N4 --tasks-per-node=4 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,4,3,1],[0,4,1,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=manual works' '
	id=$(flux submit \
	     --taskmap=manual:"[[1,3,4,1],[0,1,4,1]]" \
	     -N4 --tasks-per-node=4 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[1,3,4,1],[0,1,4,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=manual can redistribute tasks amongst nodes' '
	# put 2 extra tasks on rank 0
	id=$(flux submit \
	     --taskmap=manual:"[[0,1,3,1],[1,3,1,1]]" \
	     -N4 -n6 ./map.sh) &&
	flux job wait-event -p exec -f json $id shell.start \
		| jq -e ".context.taskmap.map == [[0,1,3,1],[1,3,1,1]]" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=manual with unknown taskmap fails' '
	test_must_fail_or_be_terminated \
		flux run --taskmap="manual:[]" true
'
test_expect_success 'invalid taskmap causes job failure' '
	test_must_fail_or_be_terminated \
		flux run -vvv --taskmap=foo true
'
test_expect_success 'invalid manual taskmaps fail predictably' '
	ids=$(flux bulksubmit --taskmap=manual:{} \
		-N4 --tasks-per-node=4 true ::: \
		"{}" \
		"[[1,3,4,1]]" \
		"[[1,3,4,1],[0,1,3,1]]") &&
	for id in $ids; do
		test_must_fail_or_be_terminated flux job attach $id
	done
'
test_expect_success 'invalid taskmap shell option fails' '
	test_must_fail_or_be_terminated \
		flux run -o taskmap.foo=bar true
'
test_expect_success 'invalid taskmap scheme causes job failure' '
	test_must_fail_or_be_terminated \
		flux run -vvv --taskmap=foo true
'
test_expect_success 'invalid cyclic value causes job failure' '
	test_must_fail_or_be_terminated \
		flux run -vvv --taskmap=cyclic:0 true
'
test_expect_success 'invalid cyclic value causes job failure' '
	test_must_fail_or_be_terminated \
		flux run -vvv --taskmap=cyclic:foo true
'

INITRC_PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"
test_expect_success 'shell supports dynamically loaded taskmap plugin' '
	cat <<-EOF >taskmap-initrc.lua &&
	plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "taskmap-reverse.so" }
	EOF
	id=$(flux submit -o userrc=taskmap-initrc.lua \
		-N4 --tasks-per-node=4 \
		--taskmap=reverse ./map.sh) &&
	flux job taskmap $id  &&
	test $(flux job taskmap $id) \
		= "[[3,1,4,1],[2,1,4,1],[1,1,4,1],[0,1,4,1]]" && \
	test_check_taskmap $id
'
test_done
