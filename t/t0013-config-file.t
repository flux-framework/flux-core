#!/bin/sh
#

test_description='Test config file overlay bootstrap'

. `dirname $0`/sharness.sh

TCONFDIR=${FLUX_SOURCE_DIR}/t/conf.d

# Avoid loading unnecessary modules in back to back broker tests
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

#
# check boot.method
#

test_expect_success 'flux broker with explicit PMI boot method works' '
	flux broker ${ARGS} -Sboot.method=pmi /bin/true
'

test_expect_success 'flux broker with unknown boot method fails' '
	test_must_fail flux broker ${ARGS} -Sboot.method=badmethod /bin/true
'

#
# check config file parsing
#

test_expect_success 'broker startup with missing config fails' "
	! FLUX_CONF_DIR=/noexist \
		flux broker ${ARGS} /bin/true
"

test_expect_success 'broker startup with invalid TOML fails' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-toml \
		flux broker ${ARGS} /bin/true
"

test_expect_success 'bootstrap config with missing bootstrap table fails' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-nobootstrap \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'bootstrap config with missing endpoints array fails' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-noendpoints \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'bootstrap config with bad endpoints array' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-intendpoints2 \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'bootstrap config with bad endpoints array element' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-intendpoints \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'bootstrap config with negative rank fails' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-rank \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'bootstrap config with >= size rank fails' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-rank2 \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

# N.B. set short shutdown grace to speed up test, as in t0001-basic

#
# size=1 boot from config file
# N.B. "private" config sets rank and optionally size, while "shared"
# config requires broker to infer rank from position of a local interface's
# IP address in the tbon-endpoints array (which only works for size=1 in
# a single-node test).
#

test_expect_success 'start size=1 with shared config file, expected attrs set' "
	FLUX_CONF_DIR=${TCONFDIR}/shared \
		flux broker ${ARGS} -Sboot.method=config \
		--shutdown-grace=0.1 \
		flux lsattr -v >1s.out &&
	grep -q 'tbon.endpoint[ ]*tcp://127.0.0.1:8500$' 1s.out
"

test_expect_success 'start size=1 with shared config file, ipc endpoint' "
	FLUX_CONF_DIR=${TCONFDIR}/shared_ipc \
		flux broker ${ARGS} -Sboot.method=config \
		--shutdown-grace=0.1 \
		/bin/true
"

test_expect_success 'start size=1 with shared config file, no endpoint' "
	! FLUX_CONF_DIR=${TCONFDIR}/shared_none \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'start size=1 with private config file' "
	FLUX_CONF_DIR=${TCONFDIR}/private \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

#
# size=2 boot from config file
#

test_expect_success NO_CHAIN_LINT 'start size=2 with private config files' '
	FLUX_CONF_DIR=${TCONFDIR}/priv2-1 \
		flux broker ${ARGS} -Sboot.method=config &
	FLUX_CONF_DIR=${TCONFDIR}/priv2-0 \
		flux broker ${ARGS} -Sboot.method=config \
		--shutdown-grace=0.1 \
		flux getattr size >2p.out &&
	echo 2 >2p.exp &&
	test_cmp 2p.exp 2p.out
'

test_done
