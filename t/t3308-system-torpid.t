#!/bin/sh
#

test_description='Check torpid broker detection
'

. `dirname $0`/sharness.sh

test_under_flux 2 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"
groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

test_expect_success 'tell brokers to log to stderr' '
	flux exec flux setattr log-stderr-mode local
'
test_expect_success 'load heartbeat module with fast rate for testing' '
        flux module reload heartbeat period=0.5s
'

test_expect_success 'tbon.torpid max/min have expected values' '
	TMIN=$(flux getattr tbon.torpid_min) &&
	TMAX=$(flux getattr tbon.torpid_max) &&
	test "$TMAX" = "30s" &&
	test "$TMIN" = "5s"
'

test_expect_success 'tbon.torpid min/max can be set on live system' '
	flux setattr tbon.torpid_max 60s &&
	TMAX=$(flux getattr tbon.torpid_max) &&
	test "$TMAX" = "1m" &&
	flux setattr tbon.torpid_min 30s &&
	TMIN=$(flux getattr tbon.torpid_min) &&
	test "$TMIN" = "30s"
'

test_expect_success 'tbon.torpid min/max cannot be set to non-FSD value' '
	test_must_fail flux setattr tbon.torpid_min foo &&
	test_must_fail flux setattr tbon.torpid_max bar
'

test_expect_success 'tbon.torpid_min cannot be set to zero' '
	test_must_fail flux setattr tbon.torpid_min 0
'

test_expect_success 'torpid_min can be set via config' '
	mkdir -p conf.d &&
	cat >conf.d/tbon.toml <<-EOT &&
	tbon.torpid_min = "6s"
	EOT
	TMIN=$(FLUX_CONF_DIR=conf.d flux start flux getattr tbon.torpid_min) &&
	test "$TMIN" = "6s"
'

test_expect_success 'torpid_min cannot be set to 0 via config' '
	mkdir -p conf.d &&
	cat >conf.d/tbon.toml <<-EOT &&
	tbon.torpid_min = "0"
	EOT
	test_must_fail bash -c "FLUX_CONF_DIR=conf.d flux start \
		flux getattr tbon.torpid_min"
'

test_expect_success 'torpid_max can be set to 0 via config' '
	mkdir -p conf.d &&
	cat >conf.d/tbon.toml <<-EOT &&
	tbon.torpid_max = "0"
	EOT
	TMAX=$(FLUX_CONF_DIR=conf.d flux start flux getattr tbon.torpid_max) &&
	test "$TMAX" = "0s"
'

# tbon.torpid_min should be >= sync_min (1s hardwired)
test_expect_success 'reduce tbon.torpid max/min values for testing' '
	flux exec flux setattr tbon.torpid_min 1s &&
	flux exec flux setattr tbon.torpid_max 2s
'

test_expect_success 'kill -STOP broker 1' '
	$startctl kill 1 19
'

test_expect_success 'rank 1 is added to broker.torpid group' '
	$groups waitfor --count=1 broker.torpid
'

test_expect_success 'kill -CONT broker 1' '
	$startctl kill 1 18
'

test_expect_success 'rank 1 is removed from broker.torpid group' '
	$groups waitfor --count=0 broker.torpid
'

test_expect_success 'set tbon.torpid_max to impossible to attain value' '
	flux setattr tbon.torpid_max 0.1s
'

test_expect_success 'rank 1 is added to broker.torpid group' '
	$groups waitfor --count=1 broker.torpid
'

test_expect_success 'flux resource list shows one node down' '
	test $(flux resource list -s down -no {nnodes}) -eq 1
'
test_expect_success 'scheduler shows one node down' '
	test $(FLUX_RESOURCE_LIST_RPC=sched.resource-status flux resource list -s down -no {nnodes}) -eq 1
'

test_expect_success 'set tbon.torpid_max to zero to disable' '
	flux setattr tbon.torpid_max 0
'

test_expect_success 'rank 1 is removed from broker.torpid group' '
	$groups waitfor --count=0 broker.torpid
'

test_expect_success 'flux resource list shows no nodes down' '
	test $(flux resource list -s down -no {nnodes}) -eq 0
'

test_expect_success 'scheduler shows no nodes down' '
	test $(FLUX_RESOURCE_LIST_RPC=sched.resource-status flux resource list -s down -no {nnodes}) -eq 0
'

test_expect_success 'no nodes are drained' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0
'

test_done
