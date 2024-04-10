#!/bin/sh
#

test_description='Test instance recovery mode'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

runpty="flux ${SHARNESS_TEST_SRCDIR}/scripts/runpty.py"

# N.B. Increase test-exit-timeout from default of 20s, on slower / busy
# systems timeout can trigger and kill broker.
test_expect_success 'start a persistent instance of size 4' '
	mkdir -p test1 &&
	flux start --test-size=4 --test-exit-timeout=300s \
		-o,-Sstatedir=$(pwd)/test1 /bin/true
'
test_expect_success 'expected broker attributes are set in recovery mode' '
	cat >recov_attrs.exp <<-EOT &&
	1
	1
	5
	EOT
	flux start --recovery=$(pwd)/test1 \
	    -o,-Sbroker.rc1_path= \
	    -o,-Sbroker.rc3_path= \
	    bash -c " \
		flux getattr broker.recovery-mode && \
	        flux getattr broker.quorum && \
	        flux getattr log-stderr-level" >recov_attrs.out &&
	test_cmp recov_attrs.exp recov_attrs.exp
'
test_expect_success 'banner message is printed in interactive recovery mode' '
	run_timeout --env=SHELL=/bin/sh 120 \
	    $runpty -i none flux start \
	        -o,-Sbroker.rc1_path= \
	        -o,-Sbroker.rc3_path= \
	        --recovery=$(pwd)/test1 >banner.out &&
	grep "Entering Flux recovery mode" banner.out
'
test_expect_success 'rc1 failure is ignored in recovery mode' '
	flux start --recovery=$(pwd)/test1 \
	    -o,-Sbroker.rc1_path=/bin/false \
	    -o,-Sbroker.rc3_path= \
	    echo "hello world" >hello.out &&
	grep hello hello.out
'
test_expect_success 'resources are offline in recovery mode' '
	echo 4 >down.exp &&
	flux start --recovery=$(pwd)/test1 \
	    flux resource list -s down -no {nnodes} >down.out &&
	test_cmp down.exp down.out
'
test_expect_success 'dump test instance in recovery mode' '
	flux start --recovery=$(pwd)/test1 \
	    flux dump --checkpoint test1.tar
'
test_expect_success 'recovery mode also works with dump file' '
	flux start --recovery=$(pwd)/test1.tar \
	    flux resource list -s down -no {nnodes} >down_dump.out &&
	test_cmp down.exp down_dump.out
'
test_expect_success 'banner message warns changes are not persistent' '
	run_timeout --env=SHELL=/bin/sh 120 \
	    $runpty -i none flux start \
	        -o,-Sbroker.rc1_path= \
	        -o,-Sbroker.rc3_path= \
	        --recovery=$(pwd)/test1.tar >banner2.out &&
	grep "changes will not be preserved" banner2.out
'
test_expect_success 'recovery mode aborts early if statedir is missing' '
	test_must_fail flux start --recovery=$(pwd)/test2 true \
		2>dirmissing.err &&
	grep "No such file or directory" dirmissing.err
'
test_expect_success 'recovery mode aborts early if statedir lacks rwx' '
	mkdir -p test2 &&
	chmod 600 test2 &&
	test_must_fail flux start --recovery=$(pwd)/test2 true \
		2>norwx.err &&
	grep "no access" norwx.err
'
test_expect_success 'recovery mode aborts early if content is missing' '
	chmod 700 test2 &&
	test_must_fail flux start --recovery=$(pwd)/test2 true \
		2>empty.err &&
	grep "No such file or directory" empty.err
'
#  Note: some environments (such as fakeroot) do not appear to respect
#  file permissions. In this case a read-only file is writeable and a
#  write-only file may be readable, which will cause the following two
#  tests to fail. Check for this condition and set a prereq that perms
#  are working:
#
touch test_perms
chmod 400 test_perms
echo test >test_perms || test_set_prereq WORKING_PERMS

test_expect_success WORKING_PERMS 'recovery mode aborts early if content unwritable' '
	touch test2/content.sqlite &&
	chmod 400 test2/content.sqlite &&
	test_must_fail flux start --recovery=$(pwd)/test2 true \
		2>nowrite.err &&
	grep "no write permission" nowrite.err
'
test_expect_success WORKING_PERMS 'recovery mode aborts early if content unreadable' '
	chmod 200 test2/content.sqlite &&
	test_must_fail flux start --recovery=$(pwd)/test2 true \
		2>noread.err &&
	grep "no read permission" noread.err
'

test_done
