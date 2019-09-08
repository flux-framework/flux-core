#!/bin/sh
#

test_description='Test flux-proxy'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

export TEST_URI=$FLUX_URI
export TEST_SOCKDIR=$(echo $FLUX_URI | sed -e "s!local://!!") &&
export TEST_FLUX=${FLUX_BUILD_DIR}/src/cmd/flux
export TEST_TMPDIR=${TMPDIR:-/tmp}

test_expect_success 'flux-proxy creates new socket' '
	PROXY_URI=$(flux proxy $TEST_URI printenv FLUX_URI) &&
	test "$PROXY_URI" != "$TEST_URI"
'

test_expect_success 'flux-proxy cleans up socket' '
	PROXY_URI=$(flux proxy $TEST_URI printenv FLUX_URI) &&
	path=$(echo $PROXY_URI | sed -e "s!local://!!") &&
	! test -d $path
'

test_expect_success 'flux-proxy exits with command return code' '
	flux proxy $TEST_URI /bin/true &&
	! flux proxy $TEST_URI /bin/false
'

test_expect_success 'flux-proxy forwards getattr request' '
	ATTR_SIZE=$(flux proxy $TEST_URI flux getattr size) &&
	test "$ATTR_SIZE" = "$SIZE"
'

test_expect_success 'flux-proxy manages event redistribution' '
	flux proxy $TEST_URI \
	  "flux event sub -c1 hb& flux event sub -c1 hb& wait;wait" &&
	FLUX_URI=$TEST_URI flux dmesg | sed -e "s/[^ ]* //" >event.out &&
	test $(egrep "connector-local.*debug\[0\]: subscribe hb" event.out|wc -l) -eq 1 &&
	test $(egrep "proxy.*debug\[0\]: subscribe hb" event.out|wc -l) -eq 1 &&
	test $(egrep "connector-local.*debug\[0\]: unsubscribe hb" event.out|wc -l) -eq 1 &&
	test $(egrep "proxy.*debug\[0\]: unsubscribe hb" event.out|wc -l) -eq 1
'

test_done
