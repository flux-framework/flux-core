#!/bin/sh
#

test_description='Test flux-kvs copy
Test flux-kvs copy and flux-kvs move.
'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

SIZE=1
test_under_flux ${SIZE} kvs

# kvs-copy value

test_expect_success 'kvs-copy value works' '
        flux kvs unlink -Rf test &&
	flux kvs put test.src=foo &&
        flux kvs copy test.src test.dst
'
test_expect_success 'kvs-copy value does not unlink src' '
	value=$(flux kvs get test.src) &&
	test "$value" = "foo"
'
test_expect_success 'kvs-copy value dst contains expected value' '
	value=$(flux kvs get test.dst) &&
	test "$value" = "foo"
'

# kvs-move value

test_expect_success 'kvs-move value works' '
        flux kvs unlink -Rf test &&
	flux kvs put test.src=bar &&
        flux kvs move test.src test.dst
'
test_expect_success 'kvs-move value unlinks src' '
	test_must_fail flux kvs get test.src
'
test_expect_success 'kvs-move value dst contains expected value' '
	value=$(flux kvs get test.dst) &&
	test "$value" = "bar"
'

# kvs-copy dir

test_expect_success 'kvs-copy dir works' '
        flux kvs unlink -Rf test &&
	flux kvs put test.src.a.b.c=foo &&
        flux kvs copy test.src test.dst
'
test_expect_success 'kvs-copy dir does not unlink src' '
	value=$(flux kvs get test.src.a.b.c) &&
	test "$value" = "foo"
'
test_expect_success 'kvs-copy dir dst contains expected value' '
	value=$(flux kvs get test.dst.a.b.c) &&
	test "$value" = "foo"
'

# kvs-move dir

test_expect_success 'kvs-move dir works' '
        flux kvs unlink -Rf test &&
	flux kvs put test.src.a.b.c=bar &&
        flux kvs move test.src test.dst
'
test_expect_success 'kvs-move dir unlinks src' '
	test_must_fail flux kvs get --treeobj test.src
'
test_expect_success 'kvs-move dir dst contains expected value' '
	value=$(flux kvs get test.dst.a.b.c) &&
	test "$value" = "bar"
'

# from namespace, via nsprefix
#   copy a value, move a dir

test_expect_success 'create test namespace' '
	flux kvs namespace-create fromns-prefix
'

test_expect_success 'kvs-copy from namespace works' '
        flux kvs unlink -Rf test &&
        flux kvs unlink -Rf ns:fromns-prefix/test &&
	flux kvs put ns:fromns-prefix/test.src=foo &&
        flux kvs copy ns:fromns-prefix/test.src test.dst
'
test_expect_success 'kvs-copy from namespace does not unlink src' '
	value=$(flux kvs get ns:fromns-prefix/test.src) &&
	test "$value" = "foo"
'
test_expect_success 'kvs-copy from namespace dst contains expected value' '
	value=$(flux kvs get test.dst) &&
	test "$value" = "foo"
'

test_expect_success 'kvs-move from namespace works' '
        flux kvs unlink -Rf test &&
        flux kvs unlink -Rf ns:fromns-prefix/test &&
	flux kvs put ns:fromns-prefix/test.src.a.b.c=foo &&
        flux kvs move ns:fromns-prefix/test.src test.dst
'
test_expect_success 'kvs-move from namespace unlinks src' '
	test_must_fail flux kvs get --treeobj ns:fromns-prefix/test.src
'
test_expect_success 'kvs-move from namespace dst contains expected value' '
	value=$(flux kvs get test.dst.a.b.c) &&
	test "$value" = "foo"
'

test_expect_success 'remove test namespace' '
	flux kvs namespace-remove fromns-prefix
'

# to namespace, via nsprefix
#   copy a value, move a dir

test_expect_success 'create test namespace' '
	flux kvs namespace-create tons-prefix
'

test_expect_success 'kvs-copy to namespace works' '
        flux kvs unlink -Rf ns:tons-prefix/test &&
        flux kvs unlink -Rf test &&
	flux kvs put test.src=foo &&
        flux kvs copy test.src ns:tons-prefix/test.dst
'
test_expect_success 'kvs-copy to namespace does not unlink src' '
	value=$(flux kvs get test.src) &&
	test "$value" = "foo"
'
test_expect_success 'kvs-copy to namespace dst contains expected value' '
	value=$(flux kvs get ns:tons-prefix/test.dst) &&
	test "$value" = "foo"
'

test_expect_success 'kvs-move to namespace works' '
        flux kvs unlink -Rf ns:tons-prefix/test &&
        flux kvs unlink -Rf test &&
	flux kvs put test.src.a.b.c=foo &&
        flux kvs move test.src ns:tons-prefix/test.dst
'
test_expect_success 'kvs-move to namespace unlinks src' '
	test_must_fail flux kvs get --treeobj test.src
'
test_expect_success 'kvs-move to namespace dst contains expected value' '
	value=$(flux kvs get ns:tons-prefix/test.dst.a.b.c) &&
	test "$value" = "foo"
'

test_expect_success 'remove test namespace' '
	flux kvs namespace-remove tons-prefix
'

# from namespace, via option
#   copy a value, move a dir

test_expect_success 'create test namespace' '
	flux kvs namespace-create fromns-option
'

test_expect_success 'kvs-copy from namespace works' '
        flux kvs unlink -Rf test &&
        flux kvs --namespace=fromns-option unlink -Rf test &&
	flux kvs --namespace=fromns-option put test.src=foo &&
        flux kvs copy --src-namespace=fromns-option test.src test.dst
'
test_expect_success 'kvs-copy from namespace does not unlink src' '
	value=$(flux kvs --namespace=fromns-option get test.src) &&
	test "$value" = "foo"
'
test_expect_success 'kvs-copy from namespace dst contains expected value' '
	value=$(flux kvs get test.dst) &&
	test "$value" = "foo"
'

test_expect_success 'kvs-move from namespace works' '
        flux kvs unlink -Rf test &&
        flux kvs --namespace=fromns-option unlink -Rf test &&
	flux kvs --namespace=fromns-option put test.src.a.b.c=foo &&
        flux kvs move --src-namespace=fromns-option test.src test.dst
'
test_expect_success 'kvs-move from namespace unlinks src' '
	test_must_fail flux kvs --namespace=fromns-option get --treeobj test.src
'
test_expect_success 'kvs-move from namespace dst contains expected value' '
	value=$(flux kvs get test.dst.a.b.c) &&
	test "$value" = "foo"
'

test_expect_success 'remove test namespace' '
	flux kvs namespace-remove fromns-option
'

# to namespace, via option
#   copy a value, move a dir

test_expect_success 'create test namespace' '
	flux kvs namespace-create tons-option
'

test_expect_success 'kvs-copy to namespace works' '
        flux kvs --namespace=tons-option unlink -Rf test &&
        flux kvs unlink -Rf test &&
	flux kvs put test.src=foo &&
        flux kvs copy --dst-namespace=tons-option test.src test.dst
'
test_expect_success 'kvs-copy to namespace does not unlink src' '
	value=$(flux kvs get test.src) &&
	test "$value" = "foo"
'
test_expect_success 'kvs-copy to namespace dst contains expected value' '
	value=$(flux kvs --namespace=tons-option get test.dst) &&
	test "$value" = "foo"
'

test_expect_success 'kvs-move to namespace works' '
        flux kvs --namespace=tons-option unlink -Rf test &&
        flux kvs unlink -Rf test &&
	flux kvs put test.src.a.b.c=foo &&
        flux kvs move --dst-namespace=tons-option test.src test.dst
'
test_expect_success 'kvs-move to namespace unlinks src' '
	test_must_fail flux kvs get --treeobj test.src
'
test_expect_success 'kvs-move to namespace dst contains expected value' '
	value=$(flux kvs --namespace=tons-option get test.dst.a.b.c) &&
	test "$value" = "foo"
'

test_expect_success 'remove test namespace' '
	flux kvs namespace-remove tons-option
'

# expected failures

test_expect_success 'kvs-copy missing argument fails' '
	test_must_fail flux kvs copy foo
'
test_expect_success 'kvs-move missing argument fails' '
	test_must_fail flux kvs move foo
'
test_expect_success 'kvs-copy nonexistent src fails' '
	flux kvs unlink -Rf test.notakey &&
	test_must_fail flux kvs copy test.notakey foo
'
test_expect_success 'kvs-move nonexistent src fails' '
	flux kvs unlink -Rf test.notakey &&
	test_must_fail flux kvs move test.notakey foo
'
test_expect_success 'kvs-copy nonexistent src namespace fails' '
	flux kvs put test.foo=bar &&
	test_must_fail flux kvs copy ns:nons/test.foo foo
'
test_expect_success 'kvs-move nonexistent src namespace fails' '
	flux kvs put test.foo=bar &&
	test_must_fail flux kvs move ns:nons/test.foo foo
'
test_expect_success 'kvs-copy nonexistent dst namespace fails' '
	flux kvs put test.foo=69 &&
	test_must_fail flux kvs copy test.foo ns:nons/test.foo
'
test_expect_success 'kvs-move nonexistent dst namespace fails' '
	flux kvs put test.foo=69 &&
	test_must_fail flux kvs move test.foo ns:nons/test.foo
'

test_done
