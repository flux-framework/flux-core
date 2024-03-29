#!/bin/sh
#
test_description='Test hostfile taskmap plugin support'

. `dirname $0`/sharness.sh

# Use "system" personality to get fake hostnames for hostfile use
test_under_flux 4 system

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
test_expect_success 'taskmap=hostfile works' '
	cat <<-EOF >h1 &&
	fake3
	fake2
	fake1
	fake0
	EOF
	expected="[[3,1,1,1],[2,1,1,1],[1,1,1,1],[0,1,1,1]]" &&
	id=$(flux submit --taskmap=hostfile:h1 -N4 -n4 ./map.sh) &&
	flux job attach -vEX $id &&
	flux job wait-event -p exec -f json $id shell.start &&
        flux job wait-event -p exec -f json $id shell.start \
                | jq -e ".context.taskmap.map == $expected" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=hostfile works with multiple tasks per node' '
	cat <<-EOF >h2 &&
	fake3
	fake3
	fake2
	fake2
	fake1
	fake1
	fake0
	fake0
	EOF
	expected="[[3,1,2,1],[2,1,2,1],[1,1,2,1],[0,1,2,1]]" &&
	id=$(flux submit --taskmap=hostfile:h2 -N4 --tasks-per-node=2 ./map.sh) &&
	flux job attach -vEX $id &&
	flux job wait-event -p exec -f json $id shell.start &&
        flux job wait-event -p exec -f json $id shell.start \
                | jq -e ".context.taskmap.map == $expected" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=hostfile reuses hosts in short hostlist' '
	cat <<-EOF >h3 &&
	fake3
	fake2
	fake1
	fake0
	EOF
	expected="[[3,1,1,1],[2,1,1,1],[1,1,1,1],[0,1,1,1],[3,1,1,1],[2,1,1,1],[1,1,1,1],[0,1,1,1]]" &&
	id=$(flux submit --taskmap=hostfile:h3 -N4 --tasks-per-node=2 ./map.sh) &&
	flux job attach -vEX $id &&
	flux job wait-event -p exec -f json $id shell.start &&
        flux job wait-event -p exec -f json $id shell.start \
                | jq -e ".context.taskmap.map == $expected" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=hostfile works with hostlists' '
	cat <<-EOF >h4 &&
	fake[1,2]
	fake[3,0]
	EOF
	expected="[[1,3,1,1],[0,4,1,1],[0,1,1,1]]" &&
	id=$(flux submit --taskmap=hostfile:h4 -N4 --tasks-per-node=2 ./map.sh) &&
	flux job attach -vEX $id &&
	flux job wait-event -p exec -f json $id shell.start &&
        flux job wait-event -p exec -f json $id shell.start \
                | jq -e ".context.taskmap.map == $expected" &&
	test_check_taskmap $id
'
test_expect_success 'taskmap=hostfile fails with invalid hostlist' '
	echo "fake[0-3">h5 &&
	test_must_fail_or_be_terminated \
		flux run --taskmap=hostfile:h5 -N4 hostname
'
test_expect_success 'taskmap=hostfile fails with incorrect hosts' '
	echo "foo[0-3]">h6 &&
	test_must_fail_or_be_terminated \
		flux run --taskmap=hostfile:h6 -N4 hostname
'
test_expect_success 'taskmap=hostfile fails when not all hosts present' '
	echo "foo[0,0,1,2]">h7 &&
	test_must_fail_or_be_terminated \
		flux run --taskmap=hostfile:h7 -N4 hostname
'
test_expect_success 'taskmap=hostfile fails with invalid filename' '
	test_must_fail_or_be_terminated \
		flux run --taskmap=hostfile:badfile -N4 hostname
'
test_done
