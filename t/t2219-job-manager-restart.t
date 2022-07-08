#!/bin/sh

test_description='Test flux job manager restart'

. $(dirname $0)/sharness.sh

DUMPS=${SHARNESS_TEST_SRCDIR}/job-manager/dumps

test_expect_success 'start instance with empty kvs, run one job, and dump' '
	flux start -o,-Scontent.dump=dump.tar \
	    flux mini run --env-remove=* /bin/true &&
	test -f $(pwd)/dump.tar
'

restart_flux() {
	flux start -o,-Scontent.restore=$1 /bin/true
}

test_expect_success 'verify that job manager can restart with current dump' '
	restart_flux dump.tar
'

for dump in ${DUMPS}/valid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'successfully started from '$testname "restart_flux $dump"
done

test_done
