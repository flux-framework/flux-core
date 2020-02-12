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

test_expect_success '[bootstrap] config with missing bootstrap table fails' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-nobootstrap \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success '[bootstrap] config with bad hosts array' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-hosts2 \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success '[bootstrap] config with bad hosts array element' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-hosts \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success '[bootstrap] config with hostname not found' "
	! FLUX_CONF_DIR=${TCONFDIR}/bad-nomatch \
		flux broker ${ARGS} -Sboot.method=config /bin/true
"

test_expect_success 'start instance with missing hosts' "
	FLUX_CONF_DIR=${TCONFDIR}/good-nohosts \
		flux broker ${ARGS} -Sboot.method=config \
		flux lsattr -v >attr.out &&
	grep 'tbon.endpoint.*-$' attr.out
"

test_expect_success 'start instance with empty hosts' "
	FLUX_CONF_DIR=${TCONFDIR}/good-emptyhosts \
		flux broker ${ARGS} -Sboot.method=config \
		flux lsattr -v >attr.out &&
	grep 'tbon.endpoint.*-$' attr.out
"

# Usage: start_broker config hostname cmd ...
start_broker() {
	local dir=$1; shift
	local host=$1; shift
	FLUX_CONF_DIR=$dir FLUX_FAKE_HOSTNAME=$host \
		flux broker ${ARGS} -Sboot.method=config "$@" &
}

test_expect_success 'start size=2 instance with ipc://' "
	start_broker ${TCONFDIR}/good-ipc2 fake0 flux getattr size >ipc.out &&
	start_broker ${TCONFDIR}/good-ipc2 fake1 &&
	wait &&
	wait &&
	echo 2 >ipc.exp &&
	test_cmp ipc.exp ipc.out
"

test_expect_success 'start size=4 instance with tcp://' "
	start_broker ${TCONFDIR}/good-tcp4 fake0 flux getattr size >tcp.out &&
	start_broker ${TCONFDIR}/good-tcp4 fake1 &&
	start_broker ${TCONFDIR}/good-tcp4 fake2 &&
	start_broker ${TCONFDIR}/good-tcp4 fake3 &&
	wait && wait && wait && wait &&
	echo 4 >tcp.exp &&
	test_cmp tcp.exp tcp.out
"


test_done
