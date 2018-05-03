#!/bin/sh
#

test_description='Test flux-hostlist functionality for nested instances'

. `dirname $0`/sharness.sh

test_under_flux 4

test_expect_success 'flux-hostlist: rc1 set resource.hosts' '
	flux kvs get --json resource.hosts >resource.hosts &&
	test_debug "cat resource.hosts"
'

test_expect_success 'flux-hostlist: works' '
	flux hostlist | tee hostlist.hosts &&
	test "$(cat hostlist.hosts | wc -l)" = 4
'

test_expect_success 'flux-hostlist: works for jobs' '
	flux wreckrun -N2 /bin/true &&
	flux hostlist $(flux wreck last-jobid) > hostlist.job &&
	test "$(cat hostlist.job | wc -l)" = 2
'
test_expect_success 'flux-hostlist: -c works' '
	flux wreckrun -N4 /bin/true &&
	flux hostlist -c $(flux wreck last-jobid) > hostlist.job-c &&
	test_cmp resource.hosts hostlist.job-c
'
test_expect_success 'flux-hostlist: -r works' '
	flux hostlist -r >hosts.ranks &&
	test "$(cat hosts.ranks | wc -l)" = 4 &&
	flux hostlist -r $(flux wreck last-jobid) > hosts.job-ranks &&
	test_cmp hosts.ranks hosts.job-ranks
'
test_expect_success 'flux-hostlist: -rc works' '
	test "$(flux hostlist -rc)" = "[0-3]" &&
	test "$(flux hostlist -rc $(flux wreck last-jobid))" = "[0-3]"
'
test_done
