#!/bin/sh

test_description='Test broker security'

. `dirname $0`/sharness.sh

# Start out with empty "config object"
export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'connector-local starts with private access policy' '
	flux dmesg | grep connector-local >dmesg.out &&
	grep allow-root-owner=false dmesg.out &&
	grep allow-guest-user=false dmesg.out
'

test_expect_success 'connector-local adds allow-root-owner on reconfig' '
	flux dmesg --clear &&
	cat >access.toml <<-EOT &&
	[access]
	allow-root-owner = true
	EOT
	flux config reload &&
	flux dmesg | grep connector-local >dmesg2.out &&
	grep allow-root-owner=true dmesg2.out
'

test_expect_success 'connector-local drops allow-root-owner on reconfig' '
	flux dmesg --clear &&
	cat >access.toml <<-EOT &&
	[access]
	EOT
	flux config reload &&
	flux dmesg | grep connector-local >dmesg3.out &&
	grep allow-root-owner=false dmesg3.out
'

test_expect_success 'connector-local adds allow-guest-user on reconfig' '
	flux dmesg --clear &&
	cat >access.toml <<-EOT &&
	[access]
	allow-guest-user = true
	EOT
	flux config reload &&
	flux dmesg | grep connector-local >dmesg4.out &&
	grep allow-guest-user=true dmesg4.out
'

test_expect_success 'connector-local drops allow-guest-user on reconfig' '
	flux dmesg --clear &&
	cat >access.toml <<-EOT &&
	[access]
	EOT
	flux config reload &&
	flux dmesg | grep connector-local >dmesg5.out &&
	grep allow-guest-user=false dmesg5.out
'

test_expect_success 'connector-local reconfig fails on unknown access key' '
	cat >access.toml <<-EOT &&
	[access]
	foo = 42
	EOT
	test_must_fail flux config reload 2>reload.err &&
	grep foo reload.err
'

test_expect_success 'reconfig with bad TOML fails' '
	cat >access.toml <<-EOT &&
	[access]
	foo
	EOT
	test_must_fail flux config reload
'

test_expect_success 'connector-local restored private access policy' '
	flux dmesg --clear &&
	cat >access.toml <<-EOT &&
	[access]
	EOT
	flux config reload &&
	flux dmesg | grep connector-local >dmesg6.out &&
	grep allow-root-owner=false dmesg6.out &&
	grep allow-guest-user=false dmesg6.out
'

test_expect_success 'simulated local connector auth failure returns EPERM' '
	flux getattr size &&
	flux module debug --set 1 connector-local &&
	test_must_fail flux getattr size 2>authfail.out &&
	grep -q "Operation not permitted" authfail.out
'

test_expect_success 'flux ping --userid displays userid' '
	flux ping --count=1 --userid broker >ping.out &&
	grep -q "userid=$(id -u) rolemask=0x5" ping.out
'

test_expect_success 'FLUX_HANDLE_USERID can spoof userid in message' '
	FLUX_HANDLE_USERID=9999 flux ping --count=1 --userid broker >ping2.out &&
	grep -q "userid=9999 rolemask=0x5" ping2.out
'

test_expect_success 'FLUX_HANDLE_ROLEMASK can spoof rolemask in message' '
	FLUX_HANDLE_ROLEMASK=0xf flux ping --count=1 --userid broker >ping3.out &&
	grep -q "userid=$(id -u) rolemask=0xf" ping3.out
'

test_expect_success 'flux ping allowed for non-owner' '
	FLUX_HANDLE_ROLEMASK=0x2 flux ping --count=1 --userid broker >ping4.out &&
	grep -q "userid=$(id -u) rolemask=0x2" ping4.out
'

test_expect_success 'flux getattr allowed for non-owner' '
	FLUX_HANDLE_ROLEMASK=0x2 flux getattr rank >rank.out &&
	grep -q "0" rank.out
'

test_expect_success 'flux setattr allowed for owner' '
	flux setattr log-stderr-level 7 &&
	test $(flux getattr log-stderr-level) -eq 7
'

test_expect_success 'flux setattr NOT allowed for non-owner' '
	! FLUX_HANDLE_ROLEMASK=0x2 flux setattr log-stderr-level 6 &&
	test $(flux getattr log-stderr-level) -eq 7
'

test_expect_success 'flux logger not allowed for non-owner' '
	MSG="hello-unauthorized-world $$" &&
	FLUX_HANDLE_ROLEMASK=0x2 flux logger $MSG &&
	! flux dmesg | grep -q $MSG

'

test_expect_success 'flux dmesg not allowed for non-owner' '
	! FLUX_HANDLE_ROLEMASK=0x2 flux dmesg 2>dmesg.err &&
	grep -q "Request requires owner credentials" dmesg.err
'

# Note these rules:
# - the dispatcher default policy is to suppress messages that do
#   not have FLUX_ROLE_OWNER (does not apply to direct flux_recv)
# - the local connector only forwards "private" messages if the connection
#   has FLUX_ROLE_OWNER or connection has userid matching message sender
# Note special test hooks:
# - the local connector allows client to set arbitrary rolemask/userid
#   if connection was authenticated with FLUX_ROLE_OWNER
# - the DEBUG_OWNERDROP_ONESHOT(4) bit forces the next OWNER connection
#   to set connection's rolemask=FLUX_ROLE_USER, userid=FLUX_USERID_UNKNOWN

# event-trace.lua registers a reactor handler thus uses the dispatcher, while
# event-trace-bypass.lua calls f:recv_event() bypasses the dispatcher policy.

test_expect_success 'connector delivers owner event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		test test.end \
                "flux event pub test.a; \
                 flux event pub test.end" >ev00.out &&
	grep -q test.a ev00.out
'

test_expect_success 'dispatcher delivers owner event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		test test.end \
                "flux event pub test.a; \
                 flux event pub test.end" >ev0.out &&
	grep -q test.a ev0.out
'

test_expect_success 'connector delivers guest event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		test test.end \
                "FLUX_HANDLE_ROLEMASK=0x2 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev1.out &&
	grep -q test.a ev1.out
'

test_expect_success 'dispatcher suppresses guest event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		test test.end \
                "FLUX_HANDLE_ROLEMASK=0x2 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev2.out &&
	! grep -q test.a ev2.out
'

test_expect_success 'connector delivers owner event to guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		test test.end \
                 "FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.a; \
                  FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev3.out &&
	grep -q test.a ev3.out
'

test_expect_success 'dispatcher delivers owner event to guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		test test.end \
                "FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev4.out &&
	grep -q test.a ev4.out
'

test_expect_success 'connector delivers guest event to other guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		test test.end \
		"FLUX_HANDLE_USERID=42 \
                 FLUX_HANDLE_ROLEMASK=0x2 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev5.out &&
	grep -q test.a ev5.out
'

test_expect_success 'dispatcher suppresses guest event to other guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		test test.end \
		"FLUX_HANDLE_USERID=42 \
                 FLUX_HANDLE_ROLEMASK=0x2 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev6.out &&
	! grep -q test.a ev6.out
'

test_expect_success 'connector delivers guest event to same guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		test test.end \
		"FLUX_HANDLE_USERID=4294967295 \
                 FLUX_HANDLE_ROLEMASK=0x2 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev7.out &&
	grep -q test.a ev7.out
'

test_expect_success 'dispatcher suppresses guest event to same guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		test test.end \
		"FLUX_HANDLE_USERID=4294967295 \
                 FLUX_HANDLE_ROLEMASK=0x2 flux event pub test.a; \
                 FLUX_HANDLE_ROLEMASK=0x1 flux event pub test.end" >ev8.out &&
	! grep -q test.a ev8.out
'

test_expect_success 'guests may add userid-prefixed services' '
	USERID=$(id -u) &&
	jq -n "{service: \"${USERID}-sectest\"}" >user_service.json &&
	FLUX_HANDLE_ROLEMASK=0x2 ${RPC} service.add <user_service.json
'

test_expect_success 'guests may not add other-userid-prefixed services' '
	USERID=$(($(id -u)+1)) &&
	jq -n "{service: \"${USERID}-sectest\"}" >user2_service.json &&
	(export FLUX_HANDLE_ROLEMASK=0x2 &&
		test_expect_code 1 ${RPC} service.add \
			<user2_service.json 2>uservice_add.err
        ) &&
	grep "Operation not permitted" uservice_add.err
'

test_expect_success 'guests may not add non-userid-prefixed services' '
	jq -n "{service: \"sectest\"}" >service.json &&
	(export FLUX_HANDLE_ROLEMASK=0x2 &&
		test_expect_code 1 ${RPC} service.add \
			<service.json 2>service_add.err
	) &&
	grep "Operation not permitted" service_add.err
'

# kvs.namespace-<NS>-setroot is a "private" event

test_expect_success 'loaded content module' '
	flux module load content
'
test_expect_success 'loaded kvs module' '
	flux module load kvs
'

test_expect_success 'connector delivers kvs.namespace-primary-setroot event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		kvs kvs.test.end \
                "flux event pub kvs.test.a; \
                 flux kvs put ev9=42; \
                 flux event pub kvs.test.end" >ev9.out &&
	grep -q kvs.namespace-primary-setroot ev9.out
'

test_expect_success 'dispatcher delivers kvs.namespace-primary-setroot event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		kvs kvs.test.end \
                "flux event pub kvs.test.a; \
                 flux kvs put ev10=42; \
                 flux event pub kvs.test.end" >ev10.out &&
	grep -q kvs.namespace-primary-setroot ev10.out
'

test_expect_success 'connector suppresses kvs.namespace-primary-setroot event to guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		kvs kvs.test.end \
                "flux event pub kvs.test.a; \
                 flux kvs put ev11=42; \
                 flux event pub kvs.test.end" >ev11.out &&
	! grep -q kvs.namespace-primary-setroot ev11.out
'

test_expect_success 'unloaded kvs module' '
	flux module remove kvs
'

test_expect_success 'flux content flush not allowed for guest user' '
	! FLUX_HANDLE_ROLEMASK=0x2 flux content flush 2>content.flush.err &&
	grep -q "Operation not permitted" content.flush.err
'

test_expect_success 'store a value with content service' '
	echo Hello >content.blob &&
	flux content store <content.blob >content.blobref
'

test_expect_success 'flux content load not allowed for guest user' '
	test_must_fail bash -c "FLUX_HANDLE_ROLEMASK=0x2 \
	    flux content load $(cat content.blobref) 2>content-load.err" &&
	    grep "Request requires owner credentials" content-load.err
'

test_expect_success 'flux content store not allowed for guest user' '
	test_must_fail bash -c "FLUX_HANDLE_ROLEMASK=0x2 \
	    flux content store <content.blob 2>content-store.err" &&
	    grep "Request requires owner credentials" content-store.err
'

test_expect_success 'unloaded content module' '
	flux module remove content
'

test_expect_success 'flux module list is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 flux module list >/dev/null
'
test_expect_success 'flux module stats --rusage is open to guests (broker)' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats --rusage broker >/dev/null
'
test_expect_success 'flux module stats --rusage is open to guests (module)' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats --rusage connector-local >/dev/null
'

test_done
