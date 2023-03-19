#!/bin/sh
#

test_description='Test stats collection and sending'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile --debug
. `dirname $0`/sharness.sh

udp=$SHARNESS_TEST_SRCDIR/scripts/stats-listen.py
timeout="$SHARNESS_TEST_SRCDIR/scripts/run_timeout.py 60"
timeout5="$SHARNESS_TEST_SRCDIR/scripts/run_timeout.py 5"
plugin_i=${FLUX_BUILD_DIR}/t/stats/.libs/stats-immediate.so
plugin_b=${FLUX_BUILD_DIR}/t/stats/.libs/stats-basic.so

test_expect_success 'prefix set' '
	$timeout flux python $udp -s flux.job.state.immediate flux start \
	"flux jobtap load $plugin_i && flux run hostname"
'

test_expect_success 'multiple packets received' '
	$timeout flux python $udp -w 3 flux start \
	"flux jobtap load $plugin_i && flux run hostname"
'

test_expect_success 'validate packets immediate' '
	$timeout flux python $udp -V flux start \
	"flux jobtap load $plugin_i && flux run hostname"
'

test_expect_success 'timing packets received immediate' '
	$timeout flux python $udp -s timing flux start \
	"flux jobtap load $plugin_i && flux run hostname"
'

test_expect_success 'timing packets received basic' '
	$timeout flux python $udp -s timing flux start \
	"flux jobtap load $plugin_b && flux run hostname && sleep 1"
'

test_expect_success 'valid content-cache packets received' '
	$timeout flux python $udp -s content-cache -V flux start sleep 1
'

test_expect_success 'nothing received with no endpoint' '
	unset FLUX_FRIPP_STATSD &&
	test_expect_code 137 $timeout5 flux python $udp -n flux start
'

test_expect_success 'FLUX_FRIPP_STATSD with colectomy' '
	FLUX_FRIPP_STATSD=localhost \
		flux start true sleep 1 2>colon.err &&
	grep "parse error" colon.err
'

test_expect_success 'FLUX_FRIPP_STATSD with invalid hostname' '
	FLUX_FRIPP_STATSD=thiscantpossiblybevalid:9000 \
		flux start true sleep 1 2>host.err &&
	grep "parse error" host.err
'

test_done
