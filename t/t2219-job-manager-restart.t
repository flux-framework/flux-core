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

test_expect_success 'verify that anon queue status persists across restart' '
	flux start -o,-Scontent.dump=dump_dis.tar \
	    flux queue disable disable-restart-test &&
	flux start -o,-Scontent.restore=dump_dis.tar \
	    flux queue status >dump_dis.out &&
	grep "disabled: disable-restart-test" dump_dis.out
'

test_expect_success 'verify that named queue status persists across restart' '
	mkdir -p conf.d &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.batch]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.dump=dump_queue_dis.tar \
	    flux queue disable --queue batch xyzzy &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_dis.tar \
	    flux queue status >dump_queue_dis.out &&
	grep "^debug: Job submission is enabled" dump_queue_dis.out &&
	grep "^batch: Job submission is disabled: xyzzy" dump_queue_dis.out
'

test_expect_success 'verify that instance can restart after config change' '
	flux start -o,-Scontent.restore=dump_queue_dis.tar \
	    flux queue status >dump_queue_reconf.out &&
	grep "^Job submission is enabled" dump_queue_reconf.out
'

mkdir -p restart_dump_conf.d

restart_flux_dump() {
	flux start \
		-o,--config-path=$(pwd)/restart_dump_conf.d \
		-o,-Scontent.restore=$1 \
		flux module stats job-manager
}

for dump in ${DUMPS}/valid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'anon: successfully started from '$testname "restart_flux_dump $dump"
done

for dump in ${DUMPS}/invalid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'anon: failed on '$testname "test_must_fail restart_flux_dump $dump"
done

cat >restart_dump_conf.d/queues.toml << EOF
[queues.debug]
[queues.batch]
EOF

for dump in ${DUMPS}/valid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'named queues: successfully started from '$testname "restart_flux_dump $dump"
done

rm -f restart_dump_conf.d/queues.toml
cat >restart_dump_conf.d/queues.toml << EOF
[queues.foobar]
[queues.boobaz]
EOF

for dump in ${DUMPS}/valid/*.tar.bz2; do
    testname=$(basename $dump)
    test_expect_success 'new named queues: successfully started from '$testname "restart_flux_dump $dump"
done

test_done
