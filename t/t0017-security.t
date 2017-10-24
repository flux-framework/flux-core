#!/bin/sh

test_description='Test broker security' 

. `dirname $0`/sharness.sh

test_under_flux 4 minimal

test_expect_success 'verify fake munge encoding of messages' '
	${FLUX_BUILD_DIR}/src/test/tmunge --fake
'

test_expect_success 'simulated local connector auth failure returns EPERM' '
	flux comms info &&
	flux module debug --set 1 connector-local &&
	test_must_fail flux comms info 2>authfail.out &&
	grep -q "Operation not permitted" authfail.out
'

test_expect_success 'flux user list fails with reasonable error when userdb is not loaded' '
	test_must_fail flux user list 2>userlistfail.out &&
	grep -q "userdb module is not loaded" userlistfail.out
'

test_expect_success 'flux userdb includes only instance owner by default' '
	flux module load userdb &&
	flux user list >userdb.list &&
	grep -q $(id -u):owner userdb.list &&
	test $(wc -l <userdb.list) -eq 1 &&
	flux module remove userdb
'

test_expect_success '(forced) userdb lookup fails with EPERM when userdb not loaded' '
	flux module debug --set 2 connector-local &&
	test_must_fail flux comms info 2>authfail2.out &&
	grep -q "Operation not permitted" authfail2.out
'

test_expect_success '(forced) userdb lookup succeeds when userdb is loaded' '
	flux module load userdb &&
	flux module debug --set 2 connector-local &&
	flux comms info &&
	flux module remove userdb
'

test_expect_success '(forced) userdb lookup fails when instance owner is removed' '
	flux module load userdb &&
	flux user delrole $(id -u) owner &&
	! flux user lookup $(id -u) &&
	flux module debug --set 2 connector-local &&
	test_must_fail flux comms info 2>authfail3.out &&
	grep -q "Operation not permitted" authfail3.out &&
	flux module remove userdb
'

test_expect_success 'flux user addrole adds users' '
	flux module load userdb &&
	flux user addrole 1234 user &&
	flux user list >userdb2.list &&
	grep -q 1234:user userdb2.list &&
	test $(wc -l <userdb2.list) -eq 2 &&
	flux module remove userdb
'

test_expect_success 'flux user delrole removes users with no roles' '
	flux module load userdb &&
	flux user addrole 1234 user &&
	flux user delrole 1234 user &&
	flux user list >userdb2.list &&
	! grep -q 1234:user userdb2.list &&
	test $(wc -l <userdb2.list) -eq 1 &&
	flux module remove userdb
'

test_expect_success 'userdb --default-rolemask works' '
	flux module load userdb --default-rolemask=owner,user &&
	flux user delrole $(id -u) owner &&
	flux user list >userdb3.list &&
	test $(wc -l <userdb3.list) -eq 0 &&
	flux module debug --set 2 connector-local &&
	flux comms info &&
	flux user list >userdb4.list &&
	grep -q $(id -u):owner,user userdb4.list &&
	flux module remove userdb
'

test_expect_success 'flux user cannot add FLUX_USERID_UNKNOWN' '
	flux module load userdb &&
	! flux user addrole 4294967295 user 2>inval.out &&
	grep -q "invalid userid" inval.out &&
	flux module remove userdb
'

test_expect_success 'flux user can add/lookup bin user by name' '
	flux module load userdb &&
	flux user addrole bin user &&
	flux user list >userdb5.list &&
	grep -q $(id -u bin):user userdb5.list &&
	flux user lookup bin >getbin.out &&
	grep -q $(id -u bin):user getbin.out &&
	flux module remove userdb
'

test_expect_success 'flux user cannot add user with no roles' '
	flux module load userdb &&
	! flux user addrole 1234 0 &&
	flux module remove userdb
'

test_expect_success 'flux ping --userid displays userid' '
	flux ping --count=1 --userid cmb >ping.out &&
	grep -q "userid=$(id -u) rolemask=0x1" ping.out
'

test_expect_success 'FLUX_HANDLE_USERID can spoof userid in message' '
	FLUX_HANDLE_USERID=9999 flux ping --count=1 --userid cmb >ping2.out &&
	grep -q "userid=9999 rolemask=0x1" ping2.out
'

test_expect_success 'FLUX_HANDLE_ROLEMASK can spoof rolemask in message' '
	FLUX_HANDLE_ROLEMASK=0xf flux ping --count=1 --userid cmb >ping3.out &&
	grep -q "userid=$(id -u) rolemask=0xf" ping3.out
'

test_expect_success 'flux ping allowed for non-owner' '
	FLUX_HANDLE_ROLEMASK=0x2 flux ping --count=1 --userid cmb >ping4.out &&
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
	MSG="hello world $$" &&
	! FLUX_HANDLE_ROLEMASK=0x2 flux logger $MSG 2>logger.err &&
	grep -q "Operation not permitted" logger.err
'

test_expect_success 'flux dmesg not allowed for non-owner' '
	MSG="hello world $$" &&
	! FLUX_HANDLE_ROLEMASK=0x2 flux dmesg 2>dmesg.err &&
	grep -q "Operation not permitted" dmesg.err
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

# kvs.setroot is a "private" event

test_expect_success 'loaded kvs module' '
	flux module load kvs
'

test_expect_success 'connector delivers kvs.setroot event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		kvs kvs.test.end \
                "flux event pub kvs.test.a; \
                 flux kvs put --json ev9=42; \
                 flux event pub kvs.test.end" >ev9.out &&
	grep -q kvs.setroot ev9.out
'

test_expect_success 'dispatcher delivers kvs.setroot event to owner connection' '
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
		kvs kvs.test.end \
                "flux event pub kvs.test.a; \
                 flux kvs put --json ev10=42; \
                 flux event pub kvs.test.end" >ev10.out &&
	grep -q kvs.setroot ev10.out
'

test_expect_success 'connector suppresses kvs.setroot event to guest connection' '
	flux module debug --set 4 connector-local &&
	run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
		kvs kvs.test.end \
                "flux event pub kvs.test.a; \
                 flux kvs put --json ev11=42; \
                 flux event pub kvs.test.end" >ev11.out &&
	! grep -q kvs.setroot ev11.out
'

test_expect_success 'unloaded kvs module' '
	flux module remove kvs
'

test_expect_success 'flux content flush not allowed for guest user' '
	! FLUX_HANDLE_ROLEMASK=0x2 flux content flush 2>content.flush.err &&
	grep -q "Operation not permitted" content.flush.err
'

test_expect_success 'flux content load/store allowed for guest user' '
	echo Hello >content.store.value &&
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux content store <content.store.value >content.store.ref &&
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux content load $(cat content.store.ref) >content.load.value &&
	test_cmp content.store.value content.load.value
'

test_done
