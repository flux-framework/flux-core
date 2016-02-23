#!/bin/sh
#

test_description='Test api disconnect generation 
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

test_expect_success 'kvs watcher gets disconnected on client exit' '
	before_watchers=`flux comms-stats --parse "#watchers" kvs` &&
	echo "before: $before_watchers" &&
	test_expect_code 142 run_timeout 1 flux kvs watch noexist &&
	after_watchers=`flux comms-stats --parse "#watchers" kvs`
	echo "after: $after_watchers" &&
	test $before_watchers -eq $after_watchers
'

test_expect_success 'multi-node kvs watcher gets disconnected on client exit' '
	${FLUX_BUILD_DIR}/t/kvs/watch_disconnect
'


test_done
