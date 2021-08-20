#!/bin/sh
#

test_description='Test stats collection and sending'

. `dirname $0`/sharness.sh

udp=$SHARNESS_TEST_SRCDIR/scripts/stats-listen.py
timeout=$SHARNESS_TEST_SRCDIR/scripts/run_timeout.py
plugin_i=${FLUX_BUILD_DIR}/t/stats/.libs/stats-immediate.so
plugin_b=${FLUX_BUILD_DIR}/t/stats/.libs/stats-basic.so

test_expect_success 'prefix set' '
	$timeout 20 flux python $udp -s flux.job.state.immediate flux start \
	"flux jobtap load $plugin_i && flux mini run hostname"
'

test_expect_success 'multiple packets received' '
	$timeout 20 flux python $udp -w 3 flux start \
	"flux jobtap load $plugin_i && flux mini run hostname"
'

test_expect_success 'validate packets immediate' '
	$timeout 20 flux python $udp -V flux start \
	"flux jobtap load $plugin_i && flux mini run hostname"
'

test_expect_success 'timing packets received immediate' '
	$timeout 20 flux python $udp -s timing flux start \
	"flux jobtap load $plugin_i && flux mini run hostname"
'

test_expect_success 'timing packets received basic' '
	$timeout 20 flux python $udp -s timing flux start \
	"flux jobtap load $plugin_b && flux mini run hostname && sleep 1"
'

test_expect_success 'valid content-cache packets received' '
	$timeout 20 flux python $udp -s content-cache -V flux start sleep 1
'

test_expect_success 'nothing received with no endpoint' '
	unset FLUX_FRIPP_STATSD && test_expect_code 137 $timeout 5 flux python $udp -n flux start
'

test_done
