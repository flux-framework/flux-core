#!/bin/sh



test_description='Test broker attributes'

. `dirname $0`/sharness.sh

test_under_flux 1 minimal

test_expect_success 'flux getattr rank works' '
	ATTR_VAL=`flux getattr rank` &&
	test "${ATTR_VAL}" -eq 0
'
test_expect_success 'flux setattr rank fails (immutable)' '
	! flux setattr rank 42
'
test_expect_success 'flux setattr --expunge rank fails (immutable)' '
	test_must_fail flux setattr --expunge rank
'
test_expect_success 'flux getattr attrtest.nonexist fails' '
	! flux getattr nonexist
'
test_expect_success 'flux setattr works' '
	flux setattr attrtest.foo bar &&
	ATTR_VAL=`flux getattr attrtest.foo` &&
	test "${ATTR_VAL}" = "bar"
'
test_expect_success 'flux setattr -e works' '
	flux setattr -e attrtest.foo &&
	! flux getattr attrtest.foo
'
test_expect_success 'flux lsattr works' '
	flux lsattr >lsattr_out &&
	grep -q rank lsattr_out &&
	! grep -q attrtest.foo lsattr_out
'
test_expect_success 'flux lsattr -v works' '
	ATTR_VAL=$(flux lsattr -v | awk "/^rank / { print \$2 }") &&
	test "${ATTR_VAL}" -eq 0
'
test_expect_success 'flux setattr --expunge missing optarg fails' '
	test_must_fail flux setattr --expunge
'
test_expect_success 'flux lsattr with extra argument fails' '
	test_must_fail flux lsattr badarg
'
test_expect_success 'flux getattr with no attribute argument fails' '
	test_must_fail flux getattr
'

test_done
