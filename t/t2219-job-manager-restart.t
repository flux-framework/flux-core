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
	flux start -o,-Scontent.restore=$1 \
		flux module stats job-manager
}

test_expect_success 'verify that job manager can restart with current dump' '
	restart_flux dump.tar >stats.out
'
test_expect_success HAVE_JQ 'and max_jobid is greater than zero' '
	jq -e ".max_jobid > 0" <stats.out
'
test_expect_success 'delete checkpoint from dump' '
	mkdir -p tmp &&
	(cd tmp && tar -xf -) <dump.tar &&
	rm tmp/checkpoint/job-manager &&
	(cd tmp && tar -cf - *) >dump-nock.tar
'
test_expect_success 'verify that job manager can restart with modified dump' '
	restart_flux dump-nock.tar >stats-nock.out
'
test_expect_success HAVE_JQ 'and max_jobid is still greater than zero' '
	jq -e ".max_jobid > 0" <stats-nock.out
'
test_expect_success 'delete job from dump' '
	mkdir -p tmp &&
	(cd tmp && tar -xf -) <dump.tar &&
	rm -r tmp/job/* &&
	(cd tmp && tar -cf - *) >dump-nojob.tar
'
test_expect_success 'verify that job manager can restart with modified dump' '
	restart_flux dump-nojob.tar >stats-nojob.out
'
test_expect_success HAVE_JQ 'and max_jobid is still greater than zero' '
	jq -e ".max_jobid > 0" <stats-nojob.out
'

test_expect_success 'purging all jobs triggers jobid checkpoint update' '
	flux start bash -c "flux mini run --env-remove=* /bin/true && \
	    flux job purge -f --num-limit=0 && \
	    flux kvs get checkpoint.job-manager"
'

for dump in ${DUMPS}/valid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'successfully started from '$testname "restart_flux $dump"
done

for dump in ${DUMPS}/invalid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'failed on '$testname "test_must_fail restart_flux $dump"
done

test_done
