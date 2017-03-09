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

test_done
