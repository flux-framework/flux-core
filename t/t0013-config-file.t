#!/bin/sh
#

test_description='Test config file overlay bootstrap'

. `dirname $0`/sharness.sh

TCONFDIR=${FLUX_SOURCE_DIR}/t/conf.d

# Avoid loading unnecessary modules in back to back broker tests
export FLUX_RC1_PATH=
export FLUX_RC3_PATH=

#
# check boot.method
#

test_expect_success 'flux broker with explicit PMI boot method works' '
	flux broker -Sboot.method=pmi /bin/true
'

test_expect_success 'flux broker with unknown boot method fails' '
	test_must_fail flux broker -Sboot.method=badmethod /bin/true
'

#
# check boot.config_file
#

test_expect_success 'flux broker without boot.config_file fails' '
	test_must_fail flux broker -Sboot.method=config /bin/true
'

test_expect_success 'flux broker with boot.config_file=/badfile fails' '
	test_must_fail flux broker -Sboot.method=config \
		-Sboot.config_file=/badfile /bin/true
'

test_expect_success 'flux broker with boot.config_file=bad-toml fails' '
	test_must_fail flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/bad-toml.conf /bin/true
'

test_expect_success 'flux broker with missing required items fails' '
	test_must_fail flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/bad-missing.conf /bin/true
'

test_expect_success 'flux broker with boot.config_file=bad-rank fails' '
	test_must_fail flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/bad-rank.conf /bin/true
'

#
# trigger config_file boot failure due to incompat attr setting
#

test_expect_success 'flux broker with incompat attrs fails' '
	test_must_fail flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/shared.conf \
		-Ssession-id=xyz \
		/bin/true
'

# N.B. set short shutdown grace to speed up test, as in t0001-basic

#
# size=1 boot from config file
# N.B. "private" config sets rank and optionally size, while "shared"
# config requires broker to infer rank from position of a local interface's
# IP address in the tbon-endpoints array (which only works for size=1 in 
# a single-node test).
#

test_expect_success 'start size=1 with shared config file, expected attrs set' '
	run_timeout 5 flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/shared.conf \
		--shutdown-grace=0.1 \
		flux lsattr -v >1s.out &&
	grep -q "session-id[ ]*test$" 1s.out &&
	grep -q "tbon.endpoint[ ]*tcp://127.0.0.1:8500$" 1s.out &&
	grep -q "mcast.endpoint[ ]*tbon$" 1s.out
'

test_expect_success 'start size=1 with shared config file, ipc endpoint' '
	run_timeout 5 flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/shared_ipc.conf \
		--shutdown-grace=0.1 \
		/bin/true
'

test_expect_success 'start size=1 with shared config file, no endpoint' '
	test_must_fail flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/shared_none.conf \
		/bin/true
'

test_expect_success 'start size=1 with private config file' '
	run_timeout 5 flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/private.conf /bin/true
'

#
# size=2 boot from config file
#

test_expect_success NO_CHAIN_LINT 'start size=2 with private config files' '
	flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/priv2.1.conf &
	run_timeout 5 flux broker -Sboot.method=config \
		-Sboot.config_file=${TCONFDIR}/priv2.0.conf \
		--shutdown-grace=0.1 \
		flux getattr size >2p.out &&
	echo 2 >2p.exp &&
	test_cmp 2p.exp 2p.out
'

test_done
