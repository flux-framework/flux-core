#!/bin/sh
#

test_description='Test api disconnect generation 
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} kvs

# Usage: check_watchers #expected #tries
check_kvs_watchers() {
	local i n
	for i in `seq 1 $2`; do
	    n=`flux comms-stats --parse "#watchers" kvs`
	    echo "Try $i: $n"
	    test $n -eq $1 && return 0
	    sleep 1
	done
	return 1
}


test_expect_success 'kvs watcher gets disconnected on client exit' '
	before_watchers=`flux comms-stats --parse "#watchers" kvs` &&
	echo "waiters before test: $before_watchers" &&
	test_expect_code 142 run_timeout 1 flux kvs watch noexist &&
	check_kvs_watchers $before_watchers 3
'

test_expect_success 'multi-node kvs watcher gets disconnected on client exit' '
	${FLUX_BUILD_DIR}/t/kvs/watch_disconnect
'


test_done
