#!/bin/sh
#

test_description='kvs watch tests in flux session'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

DIR=test.a.b

#
# watch stress tests
#

test_expect_success 'kvs: watch-mt: multi-threaded kvs watch program' '
	${FLUX_BUILD_DIR}/t/kvs/watch mt 100 100 $DIR.a &&
	flux kvs unlink -Rf $DIR.a
'

test_expect_success 'kvs: watch-selfmod: watch callback modifies watched key' '
	${FLUX_BUILD_DIR}/t/kvs/watch selfmod $DIR.a &&
	flux kvs unlink -Rf $DIR.a
'

test_expect_success 'kvs: watch-unwatch unwatch works' '
	${FLUX_BUILD_DIR}/t/kvs/watch unwatch $DIR.a &&
	flux kvs unlink -Rf $DIR.a
'

test_expect_success 'kvs: watch-unwatchloop 1000 watch/unwatch ok' '
	${FLUX_BUILD_DIR}/t/kvs/watch unwatchloop $DIR.a &&
	flux kvs unlink -Rf $DIR.a
'

test_expect_success 'kvs: 256 simultaneous watches works' '
	${FLUX_BUILD_DIR}/t/kvs/watch simulwatch $DIR.a 256 &&
	flux kvs unlink -Rf $DIR.a
'

test_done
