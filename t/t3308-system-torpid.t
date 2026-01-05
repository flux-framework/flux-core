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

# Usage: set_torpid min max
set_torpid() {
	flux config get \
		| jq ".tbon.torpid_min = \"$1\"" \
		| jq ".tbon.torpid_max = \"$2\"" \
		| flux config load
}
# Usage: get_torpid name_suffix
get_torpid() {
	flux getattr tbon.torpid_$1
}
# Usage: set_torpid_all min max
set_torpid_all() {
	flux config get \
		| jq ".tbon.torpid_min = \"$1\"" \
		| jq ".tbon.torpid_max = \"$2\"" \
		| flux exec flux config load
}

test_expect_success 'tbon.torpid max/min have expected values' '
	test "$(get_torpid max)" = "30s" &&
	test "$(get_torpid min)" = "5s"
'

# To cover max > min, tbon.torpid_min must still be set to defaults.
# It cannot appear in the config object or the min < max code path will be taken.
test_expect_success 'tbon.torpid.max must be > min' '
	test_must_fail flux config set --type=fsd tbon.torpid_max 2s
'

test_expect_success 'tbon.torpid.min must be < max' '
	test_must_fail set_torpid 1s 0.1s
'

test_expect_success 'tbon.torpid min/max can be set on live system' '
	set_torpid 30s 60s &&
	test "$(get_torpid min)" = "30s" &&
	test "$(get_torpid max)" = "1m"
'

test_expect_success 'tbon.torpid min/max cannot be set to non-FSD value' '
	test_must_fail set_torpid 1s foo &&
	test_must_fail set_torpid bar 1s
'
test_expect_success 'tbon.torpid_min cannot be set to zero' '
	test_must_fail set_torpid 0 1s
'

test_expect_success 'torpid_min can be set via config' '
	echo 6s >tmin.exp &&
	cat >tbon.toml <<-EOT &&
	tbon.torpid_min = "6s"
	EOT
	flux start --config=tbon.toml flux getattr tbon.torpid_min >tmin.out &&
	test_cmp tmin.exp tmin.out
'

test_expect_success 'torpid_min cannot be set to 0 via config' '
	cat >tbon2.toml <<-EOT &&
	tbon.torpid_min = "0"
	EOT
	test_must_fail flux start --config=tbon2.toml true
'

test_expect_success 'torpid_max can be set to 0 (disable) via config' '
	echo 0s >tmax.exp &&
	cat >tbon3.toml <<-EOT &&
	tbon.torpid_max = "0"
	EOT
	flux start --config=tbon3.toml flux getattr tbon.torpid_max >tmax.out &&
	test_cmp tmax.exp tmax.out
'

# tbon.torpid_min should be >= sync_min (1s hardwired)
test_expect_success 'reduce tbon.torpid max/min values for testing' '
	set_torpid 1s 2s
'
test_expect_success 'kill -STOP broker 1' '
	$startctl kill 1 19
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

test_expect_success 'kill -CONT broker 1' '
	$startctl kill 1 18
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
