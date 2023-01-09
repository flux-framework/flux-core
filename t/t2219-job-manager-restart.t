#!/bin/sh

test_description='Test flux job manager restart'

. $(dirname $0)/sharness.sh

DUMPS=${SHARNESS_TEST_SRCDIR}/job-manager/dumps

export FLUX_DISABLE_JOB_CLEANUP=t

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

test_expect_success 'verify that anon queue disable persists across restart' '
	flux start -o,-Scontent.dump=dump_dis.tar \
	    flux queue disable disable-restart-test &&
	flux start -o,-Scontent.restore=dump_dis.tar \
	    flux queue status >dump_dis.out &&
	grep "disabled: disable-restart-test" dump_dis.out
'

test_expect_success 'verify that anon queue stopped persists across restart' '
	flux start -o,-Scontent.dump=dump_stopped.tar \
	    flux queue stop stop-restart-test &&
	flux start -o,-Scontent.restore=dump_stopped.tar \
	    flux queue status >dump_stopped.out &&
	grep "stopped: stop-restart-test" dump_stopped.out
'

test_expect_success 'verify that named queue enable/disable persists across restart' '
	mkdir -p conf.d &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.batch]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.dump=dump_queue_enable1.tar \
	    flux queue status >dump_queue_enable_1.out &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_enable1.tar \
	    -o,-Scontent.dump=dump_queue_enable2.tar \
	    flux queue disable --queue=batch xyzzy &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_enable2.tar \
	    -o,-Scontent.dump=dump_queue_enable3.tar \
	    flux queue status >dump_queue_enable_2.out &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_enable3.tar \
	    -o,-Scontent.dump=dump_queue_enable4.tar \
	    flux queue enable --queue=batch &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_enable4.tar \
	    -o,-Scontent.dump=dump_queue_enable5.tar \
	    flux queue status >dump_queue_enable_3.out &&
	grep "^debug: Job submission is enabled" dump_queue_enable_1.out &&
	grep "^batch: Job submission is enabled" dump_queue_enable_1.out &&
	grep "^debug: Job submission is enabled" dump_queue_enable_2.out &&
	grep "^batch: Job submission is disabled: xyzzy" dump_queue_enable_2.out &&
	grep "^debug: Job submission is enabled" dump_queue_enable_3.out &&
	grep "^batch: Job submission is enabled" dump_queue_enable_3.out
'

# N.B. no named queues configured in this test, so anon queue is what
# is tested
test_expect_success 'verify that instance can restart after config change' '
	flux start -o,-Scontent.restore=dump_queue_enable5.tar \
	    flux queue status >dump_queue_reconf.out &&
	grep "^Job submission is enabled" dump_queue_reconf.out
'

test_expect_success 'verify that named queue start/stop persists across restart' '
	rm -rf /tmp/achu/mylog &&
	mkdir -p conf.d &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.batch]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.dump=dump_queue_start1.tar \
	    flux queue status >dump_queue_start_1.out &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_start1.tar \
	    -o,-Scontent.dump=dump_queue_start2.tar \
	    flux queue stop --queue=batch xyzzy &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_start2.tar \
	    -o,-Scontent.dump=dump_queue_start3.tar \
	    flux queue status >dump_queue_start_2.out &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_start3.tar \
	    -o,-Scontent.dump=dump_queue_start4.tar \
	    flux queue start --queue=batch &&
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_start4.tar \
	    -o,-Scontent.dump=dump_queue_start5.tar \
	    flux queue status >dump_queue_start_3.out &&
	grep "^debug: Scheduling is started" dump_queue_start_1.out &&
	grep "^batch: Scheduling is started" dump_queue_start_1.out &&
	grep "^debug: Scheduling is started" dump_queue_start_2.out &&
	grep "^batch: Scheduling is stopped: xyzzy" dump_queue_start_2.out &&
	grep "^debug: Scheduling is started" dump_queue_start_3.out &&
	grep "^batch: Scheduling is started" dump_queue_start_3.out
'

test_expect_success 'checkpointed queue no longer configured on restart is ignored' '
	mkdir -p conf.d &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.batch]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.dump=dump_queue_missing.tar \
	    flux queue disable --queue batch xyzzy &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_missing.tar \
	    flux queue status >dump_queue_missing.out &&
	grep "^debug: Job submission is enabled" dump_queue_missing.out &&
	grep "^debug: Scheduling is started" dump_queue_missing.out &&
	test_must_fail grep "batch" dump_queue_missing.out
'

test_expect_success 'new queue configured on restart uses defaults' '
	mkdir -p conf.d &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.batch]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.dump=dump_queue_ignored.tar \
	    flux queue disable --queue batch xyzzy &&
	cat >conf.d/queues.toml <<-EOT &&
	[queues.debug]
	[queues.newqueue]
	EOT
	flux start -o,--config-path=$(pwd)/conf.d \
	    -o,-Scontent.restore=dump_queue_ignored.tar \
	    flux queue status >dump_queue_ignored.out &&
	grep "^debug: Job submission is enabled" dump_queue_ignored.out &&
	grep "^debug: Scheduling is started" dump_queue_ignored.out &&
	grep "^newqueue: Job submission is enabled" dump_queue_ignored.out &&
	grep "^newqueue: Scheduling is started" dump_queue_ignored.out
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
