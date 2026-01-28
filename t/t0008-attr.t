#!/bin/sh



test_description='Test broker attributes'

. `dirname $0`/sharness.sh

test_under_flux 1 minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'flux getattr rank works' '
	ATTR_VAL=`flux getattr rank` &&
	test "${ATTR_VAL}" -eq 0
'
test_expect_success 'flux setattr rank fails (immutable)' '
	test_must_fail flux setattr rank 42
'
test_expect_success 'flux getattr test.nonexist fails' '
	test_must_fail flux getattr test.nonexist
'
test_expect_success 'flux setattr works' '
	flux setattr test.foo bar &&
	ATTR_VAL=`flux getattr test.foo` &&
	test "${ATTR_VAL}" = "bar"
'
test_expect_success 'flux lsattr works' '
	flux lsattr >lsattr_out &&
	grep -q rank lsattr_out &&
	grep -q test.foo lsattr_out
'
test_expect_success 'flux lsattr -v works' '
	ATTR_VAL=$(flux lsattr -v | awk "/^rank / { print \$2 }") &&
	test "${ATTR_VAL}" -eq 0
'
test_expect_success 'flux lsattr with extra argument fails' '
	test_must_fail flux lsattr badarg
'
test_expect_success 'flux getattr with no attribute argument fails' '
	test_must_fail flux getattr
'
test_expect_success 'get request with empty payload fails with EPROTO(71)' '
	${RPC} attr.get 71 </dev/null
'
test_expect_success 'set request with empty payload fails with EPROTO(71)' '
	${RPC} attr.set 71 </dev/null
'
test_expect_success 'rm request with empty payload fails with EPROTO(71)' '
	${RPC} attr.rm 71 </dev/null
'

test_done
