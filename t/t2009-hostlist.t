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
test_expect_success 'flux-hostlist: -r works' '
	flux hostlist -r >hosts.ranks &&
	test "$(cat hosts.ranks | wc -l)" = 4
'
test_expect_success 'flux-hostlist: -rc works' '
	test "$(flux hostlist -rc)" = "[0-3]"
'
test_done
