#!/bin/sh
#

test_description='Test broker rexec functionality


Test rexec functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/subprocess

test_expect_success 'basic rexec functionality (process success)' '
	${FLUX_BUILD_DIR}/t/rexec/rexec /bin/true
'

test_must_fail 'basic rexec functionality (process fail)' '
	${FLUX_BUILD_DIR}/t/rexec/rexec /bin/false
'

test_expect_success 'basic rexec - cwd correct' '
	(cd /tmp &&
         cwd=`${FLUX_BUILD_DIR}/t/rexec/rexec pwd` &&
         test "$cwd" = "/tmp")
'

test_expect_success 'basic rexec - env passed through' '
        export FOO_BAR_BAZ=10 &&
        ${FLUX_BUILD_DIR}/t/rexec/rexec env > output &&
        grep "FOO_BAR_BAZ=10" output
'

test_expect_success 'basic rexec functionality (echo stdout)' '
	${FLUX_BUILD_DIR}/t/rexec/rexec ${TEST_SUBPROCESS_DIR}/test_echo -P -O foobar.stdout > output &&
        echo "STDOUT:foobar.stdout" > expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec functionality (echo stderr)' '
	${FLUX_BUILD_DIR}/t/rexec/rexec ${TEST_SUBPROCESS_DIR}/test_echo -P -E foobar.stderr > output 2>&1 &&
        echo "STDERR:foobar.stderr" > expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec functionality (echo stdout/err)' '
	${FLUX_BUILD_DIR}/t/rexec/rexec ${TEST_SUBPROCESS_DIR}/test_echo -O -E foobar.stdouterr > output 2>&1 &&
        echo "foobar.stdouterr" > expected &&
        echo "foobar.stdouterr" >> expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec invalid rank' '
	! ${FLUX_BUILD_DIR}/t/rexec/rexec -r 32 /bin/true > output 2>&1 &&
        grep -q "No route to host" output
'

test_expect_success 'basic rexec fail exec()' '
	! ${FLUX_BUILD_DIR}/t/rexec/rexec / > output 2>&1 &&
        grep -q "Permission denied" output
'

test_expect_success 'basic rexec fail exec() EACCES' '
	! ${FLUX_BUILD_DIR}/t/rexec/rexec / > output 2>&1 &&
        grep -q "Permission denied" output
'

test_expect_success 'basic rexec fail exec() ENOENT' '
	! ${FLUX_BUILD_DIR}/t/rexec/rexec /usr/bin/foobarbaz > output 2>&1 &&
        grep -q "No such file or directory" output
'

test_expect_success 'basic rexec propogates exit code()' '
        test_expect_code 0 ${FLUX_BUILD_DIR}/t/rexec/rexec /bin/true &&
        test_expect_code 1 ${FLUX_BUILD_DIR}/t/rexec/rexec /bin/false &&
        test_expect_code 2 ${FLUX_BUILD_DIR}/t/rexec/rexec sh -c "exit 2" &&
        test_expect_code 3 ${FLUX_BUILD_DIR}/t/rexec/rexec sh -c "exit 3"
'

test_expect_success 'basic rexec functionality (check state changes)' '
	${FLUX_BUILD_DIR}/t/rexec/rexec -s /bin/true > output &&
        echo "Started" > expected &&
        echo "Running" >> expected &&
        echo "Exited" >> expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec fail exec() (check state changes)' '
	! ${FLUX_BUILD_DIR}/t/rexec/rexec -s / > output &&
        echo "Started" > expected &&
        echo "Exec Failed" >> expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec stdin' '
	echo -n "hello" | ${FLUX_BUILD_DIR}/t/rexec/rexec -i STDIN ${TEST_SUBPROCESS_DIR}/test_echo -O -E > output 2>&1 &&
        echo "hello" > expected &&
        echo "hello" >> expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec stdin / stdout multiple lines' '
	/bin/echo -en "foo\nbar\nbaz\n" | ${FLUX_BUILD_DIR}/t/rexec/rexec -i STDIN ${TEST_SUBPROCESS_DIR}/test_echo -O -n > output 2>&1 &&
        echo "foo" > expected &&
        echo "bar" >> expected &&
        echo "baz" >> expected &&
        test_cmp expected output
'

test_expect_success 'basic rexec stdin / stdout long lines' '
        dd if=/dev/urandom bs=4096 count=1 | base64 --wrap=0 >expected &&
        ${FLUX_BUILD_DIR}/t/rexec/rexec cat expected > output &&
        test_cmp expected output
'

# pipe in /dev/null, we don't care about stdin for this test
test_expect_success 'rexec check channel FD created' '
	${FLUX_BUILD_DIR}/t/rexec/rexec -i TEST_CHANNEL /usr/bin/env < /dev/null > output 2>&1 &&
        grep "TEST_CHANNEL=" output
'

# rexec does not close TEST_CHANNEL, so we tell test_echo max
# bytes we're feeding in
test_expect_success 'rexec channel input' '
	echo -n "foobar" | ${FLUX_BUILD_DIR}/t/rexec/rexec -i TEST_CHANNEL ${TEST_SUBPROCESS_DIR}/test_echo -c TEST_CHANNEL -P -O -b 6 > output 2>&1 &&
        echo "STDOUT:foobar" > expected &&
        test_cmp expected output
'

# rexec does not close TEST_CHANNEL, so we tell test_echo max
# bytes we're feeding in
test_expect_success 'rexec channel input and output' '
	echo -n "foobaz" | ${FLUX_BUILD_DIR}/t/rexec/rexec -i TEST_CHANNEL ${TEST_SUBPROCESS_DIR}/test_echo -c TEST_CHANNEL -P -C -b 6 > output 2>&1 &&
        echo "TEST_CHANNEL:foobaz" > expected &&
        test_cmp expected output
'

# rexec does not close TEST_CHANNEL, so we tell test_echo max
# bytes we're feeding in
test_expect_success 'rexec channel input and output multiple lines' '
	/bin/echo -en "foo\nbar\nbaz\n" | ${FLUX_BUILD_DIR}/t/rexec/rexec -i TEST_CHANNEL ${TEST_SUBPROCESS_DIR}/test_echo -c TEST_CHANNEL -C -n -b 6 > output 2>&1 &&
        echo "foo" > expected &&
        echo "bar" >> expected &&
        echo "baz" >> expected &&
        test_cmp expected output
'

test_expect_success 'rexec kill' '
	${FLUX_BUILD_DIR}/t/rexec/rexec_signal /bin/sleep 10 > output 2>&1 &&
        grep "subprocess terminated by signal 15" output
'

test_expect_success 'rexec kill group' '
	${FLUX_BUILD_DIR}/t/rexec/rexec_signal ${TEST_SUBPROCESS_DIR}/test_fork_sleep 30 > output 2>&1 &&
        grep "subprocess terminated by signal 15" output
'

test_expect_success NO_CHAIN_LINT 'rexec ps works' '
        ${FLUX_BUILD_DIR}/t/rexec/rexec -r 1 sleep 100 &
        pid1=$!
        ${FLUX_BUILD_DIR}/t/rexec/rexec -r 1 sleep 100 &
        pid2=$!
	sleep 1 &&
        ${FLUX_BUILD_DIR}/t/rexec/rexec_ps -r 1 > output &&
        count=`cat output | wc -l` &&
	kill -TERM $pid1 &&
	kill -TERM $pid2 &&
        test "$count" = "2"
'

test_expect_success NO_CHAIN_LINT 'disconnect terminates all running processes' '
        ${FLUX_BUILD_DIR}/t/rexec/rexec -r 1 sleep 100 &
        pid1=$!
        ${FLUX_BUILD_DIR}/t/rexec/rexec -r 1 sleep 100 &
        pid2=$!
	sleep 1 &&
        ${FLUX_BUILD_DIR}/t/rexec/rexec_ps -r 1 > output &&
        count=`cat output | wc -l` &&
        test "$count" = "2" &&
	sleep 1 &&
	kill -TERM $pid1 &&
	kill -TERM $pid2 &&
        ${FLUX_BUILD_DIR}/t/rexec/rexec_ps -r 1 > output &&
        count=`cat output | wc -l` &&
        test "$count" = "0"
'

test_done
