#!/bin/sh

test_description='Test flux iobuf service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

IOBUFSERVICE=${FLUX_BUILD_DIR}/t/iobuf/iobuf-service
IOBUFRPC=${FLUX_BUILD_DIR}/t/iobuf/iobuf-rpc
RPC=${FLUX_BUILD_DIR}/t/request/rpc
waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

# Size the session to one more than the number of cores, minimum of 4
SIZE=4
test_under_flux ${SIZE} full

# after start iobuf service, check for "reactor ready" output to ensure it has
# started up, so we will avoid raciness with the background process
iobuf_start() {
    name=$1
    maxbuffers=$2
    eofcount=$3
    ${IOBUFSERVICE} ${name} ${maxbuffers} ${eofcount} > service-${name}.out &
    pid=$! &&
    $waitfile --quiet --count=1 --timeout=5 --pattern="reactor ready" service-${name}.out &&
    echo ${pid}
}

# send SIGTERM to the iobuf-service tool, which will inform it to cleanup properly.
# since it was executed in a sub-shell and is not our child process, use kill -0 to
# monitor for completion
iobuf_cleanup() {
    pid=$1
    kill -TERM ${pid}
    while kill -0 "$pid" > /dev/null 2>&1; do
        sleep 0.25
    done
}

#
# Basic tests
#

test_expect_success 'iobuf: basic setup and teardown' '
    pid=$(iobuf_start "iobuf1" 0 0) &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: basic write & read' '
    pid=$(iobuf_start "iobuf2" 0 0) &&
    ${IOBUFRPC} write "iobuf2" "stdout" 1 "foo" &&
    ${IOBUFRPC} read "iobuf2" "stdout" 1 > iobuf2-read.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foo" iobuf2-read.out &&
    grep "data_len: 3" iobuf2-read.out
'

test_expect_success 'iobuf: basic create & write & read' '
    pid=$(iobuf_start "iobuf3" 0 0) &&
    ${IOBUFRPC} create "iobuf3" "stdout" 1 &&
    ${IOBUFRPC} read "iobuf3" "stdout" 1 > iobuf3-read-1.out &&
    ${IOBUFRPC} write "iobuf3" "stdout" 1 "foo" &&
    ${IOBUFRPC} read "iobuf3" "stdout" 1 > iobuf3-read-2.out &&
    iobuf_cleanup ${pid} &&
    grep "data_len: 0" iobuf3-read-1.out &&
    grep "data: foo" iobuf3-read-2.out &&
    grep "data_len: 3" iobuf3-read-2.out
'

test_expect_success 'iobuf: can read buffer multiple times' '
    pid=$(iobuf_start "iobuf5" 0 0) &&
    ${IOBUFRPC} write "iobuf5" "stdout" 1 "foo" &&
    ${IOBUFRPC} read "iobuf5" "stdout" 1 > iobuf5-read-A.out &&
    ${IOBUFRPC} read "iobuf5" "stdout" 1 > iobuf5-read-B.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foo" iobuf5-read-A.out &&
    grep "data_len: 3" iobuf5-read-A.out &&
    grep "data: foo" iobuf5-read-B.out &&
    grep "data_len: 3" iobuf5-read-B.out
'

test_expect_success 'iobuf: multiple writes are appended' '
    pid=$(iobuf_start "iobuf6" 0 0) &&
    ${IOBUFRPC} write "iobuf6" "stdout" 1 "foo" &&
    ${IOBUFRPC} write "iobuf6" "stdout" 1 "bar" &&
    ${IOBUFRPC} write "iobuf6" "stdout" 1 "baz" &&
    ${IOBUFRPC} read "iobuf6" "stdout" 1 > iobuf6-read.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foobarbaz" iobuf6-read.out &&
    grep "data_len: 9" iobuf6-read.out
'

test_expect_success 'iobuf: different streams go to different buffers' '
    pid=$(iobuf_start "iobuf7" 0 0) &&
    ${IOBUFRPC} write "iobuf7" "stdout" 1 "foo" &&
    ${IOBUFRPC} write "iobuf7" "stdout" 1 "bar" &&
    ${IOBUFRPC} write "iobuf7" "stderr" 1 "foof" &&
    ${IOBUFRPC} write "iobuf7" "stderr" 1 "barf" &&
    ${IOBUFRPC} read "iobuf7" "stdout" 1 > iobuf7-read-stdout.out &&
    ${IOBUFRPC} read "iobuf7" "stderr" 1 > iobuf7-read-stderr.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foobar" iobuf7-read-stdout.out &&
    grep "data_len: 6" iobuf7-read-stdout.out &&
    grep "data: foofbarf" iobuf7-read-stderr.out &&
    grep "data_len: 8" iobuf7-read-stderr.out
'

test_expect_success 'iobuf: different ranks go to different buffers' '
    pid=$(iobuf_start "iobuf8" 0 0) &&
    ${IOBUFRPC} write "iobuf8" "stdout" 1 "foo" &&
    ${IOBUFRPC} write "iobuf8" "stdout" 1 "bar" &&
    ${IOBUFRPC} write "iobuf8" "stdout" 2 "foof" &&
    ${IOBUFRPC} write "iobuf8" "stdout" 2 "barf" &&
    ${IOBUFRPC} read "iobuf8" "stdout" 1 > iobuf8-read-1.out &&
    ${IOBUFRPC} read "iobuf8" "stdout" 2 > iobuf8-read-2.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foobar" iobuf8-read-1.out &&
    grep "data_len: 6" iobuf8-read-1.out &&
    grep "data: foofbarf" iobuf8-read-2.out &&
    grep "data_len: 8" iobuf8-read-2.out
'

test_expect_success 'iobuf: test eof' '
    pid=$(iobuf_start "iobuf9" 0 0) &&
    ${IOBUFRPC} write "iobuf9" "stdout" 1 "foo" &&
    ${IOBUFRPC} eof "iobuf9" "stdout" 1 &&
    ! ${IOBUFRPC} write "iobuf9" "stdout" 1 "foo" &&
    ${IOBUFRPC} read "iobuf9" "stdout" 1 > iobuf9-read.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foo" iobuf9-read.out &&
    grep "data_len: 3" iobuf9-read.out
'

test_expect_success 'iobuf: test eof on no-data buffer' '
    pid=$(iobuf_start "iobuf10" 0 0) &&
    ${IOBUFRPC} eof "iobuf10" "stdout" 1 &&
    ! ${IOBUFRPC} write "iobuf10" "stdout" 1 "foo" &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: test eof_count cb' '
    pid=$(iobuf_start "iobuf11" 2 2) &&
    ! grep "eof max reached" service-iobuf11.out &&
    ${IOBUFRPC} write "iobuf11" "stdout" 1 "foo" &&
    ${IOBUFRPC} write "iobuf11" "stdout" 2 "foo" &&
    ! grep "eof max reached" service-iobuf11.out &&
    ${IOBUFRPC} eof "iobuf11" "stdout" 1 &&
    ${IOBUFRPC} eof "iobuf11" "stdout" 2 &&
    $waitfile --quiet --count=1 --timeout=5 --pattern="eof max reached" service-iobuf11.out &&
    iobuf_cleanup ${pid}
'

#
# Corner case tests
#

test_expect_success 'iobuf: invalid requests fail with EPROTO(71)' '
    pid=$(iobuf_start "iobufcc1" 0 0) &&
    ${RPC} iobufcc1.create 71 </dev/null &&
    ${RPC} iobufcc1.write 71 </dev/null &&
    ${RPC} iobufcc1.read 71 </dev/null &&
    ${RPC} iobufcc1.eof 71 </dev/null &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: duplicate service name on setup results in error' '
    pid=$(iobuf_start "iobufcc2" 0 0) &&
    ! ${IOBUFSERVICE} "iobufcc2" &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: invalid name results in error' '
    pid=$(iobuf_start "iobufcc3" 0 0) &&
    ! ${IOBUFRPC} create "bad" "stdout" 1 - &&
    ! ${IOBUFRPC} write "bad" "stdout" 1 "foo" &&
    ! ${IOBUFRPC} read "bad" "stdout" 1 - &&
    ! ${IOBUFRPC} eof "bad" "stdout" 1 - &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: duplicate stream/rank on create results in error' '
    pid=$(iobuf_start "iobufcc4" 0 0) &&
    ${IOBUFRPC} create "iobufcc4" "stdout" 1 &&
    ! ${IOBUFRPC} create "iobufcc4" "stdout" 1 &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: invalid inputs on read results in error' '
    pid=$(iobuf_start "iobufcc5" 0 0) &&
    ${IOBUFRPC} write "iobufcc5" "stdout" 1 "foo" &&
    ! ${IOBUFRPC} read "iobufcc5" "bad" 1 &&
    ! ${IOBUFRPC} read "iobufcc5" "stdout" 5 &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: error creating too many buffers' '
    pid=$(iobuf_start "iobufcc6" 2 0) &&
    ${IOBUFRPC} write "iobufcc6" "stdout" 1 "foo" &&
    ${IOBUFRPC} write "iobufcc6" "stdout" 2 "foo" &&
    ! echo -n "foo" | ${IOBUFRPC} write "iobufcc6" "stdout" 3 - &&
    iobuf_cleanup ${pid}
'

test_expect_success 'iobuf: 0 length writes work' '
    pid=$(iobuf_start "iobufcc7" 0 0) &&
    ${IOBUFRPC} write "iobufcc7" "stdout" 1 "" &&
    ${IOBUFRPC} read "iobufcc7" "stdout" 1 > iobufcc7-read.out &&
    iobuf_cleanup ${pid} &&
    grep "data_len: 0" iobufcc7-read.out
'

#
# Test with flux-exec
#

test_expect_success 'iobuf: write on different ranks' '
    pid=$(iobuf_start "iobufexec1" 0 0) &&
    flux exec -r 0 ${IOBUFRPC} write "iobufexec1" "stdout" 0 "aaa" &&
    flux exec -r 1 ${IOBUFRPC} write "iobufexec1" "stdout" 1 "bbb" &&
    flux exec -r 2 ${IOBUFRPC} write "iobufexec1" "stdout" 2 "ccc" &&
    flux exec -r 3 ${IOBUFRPC} write "iobufexec1" "stdout" 3 "ddd" &&
    flux exec -r 1 ${IOBUFRPC} write "iobufexec1" "stdout" 1 "bbb" &&
    flux exec -r 2 ${IOBUFRPC} write "iobufexec1" "stdout" 2 "ccc" &&
    flux exec -r 1 ${IOBUFRPC} write "iobufexec1" "stdout" 1 "bbb" &&
    flux exec -r 0 ${IOBUFRPC} eof "iobufexec1" "stdout" 0 &&
    flux exec -r 1 ${IOBUFRPC} eof "iobufexec1" "stdout" 1 &&
    flux exec -r 2 ${IOBUFRPC} eof "iobufexec1" "stdout" 2 &&
    flux exec -r 3 ${IOBUFRPC} eof "iobufexec1" "stdout" 3 &&
    ${IOBUFRPC} read "iobufexec1" "stdout" 0 > iobufexec1-read-0.out &&
    ${IOBUFRPC} read "iobufexec1" "stdout" 1 > iobufexec1-read-1.out &&
    ${IOBUFRPC} read "iobufexec1" "stdout" 2 > iobufexec1-read-2.out &&
    ${IOBUFRPC} read "iobufexec1" "stdout" 3 > iobufexec1-read-3.out &&
    iobuf_cleanup ${pid} &&
    grep "data: aaa" iobufexec1-read-0.out &&
    grep "data_len: 3" iobufexec1-read-0.out &&
    grep "data: bbbbbbbbb" iobufexec1-read-1.out &&
    grep "data_len: 9" iobufexec1-read-1.out &&
    grep "data: cccccc" iobufexec1-read-2.out &&
    grep "data_len: 6" iobufexec1-read-2.out &&
    grep "data: ddd" iobufexec1-read-3.out &&
    grep "data_len: 3" iobufexec1-read-3.out
'

test_expect_success 'iobuf: test eof on different ranks' '
    pid=$(iobuf_start "iobufexec2" 0 0) &&
    flux-exec -r 1 ${IOBUFRPC} write "iobufexec2" "stdout" 1 "foo" &&
    flux-exec -r 1 ${IOBUFRPC} eof "iobufexec2" "stdout" 1 &&
    ! flux-exec -r 1 ${IOBUFRPC} write "iobufexec2" "stdout" 1 "foo" &&
    flux-exec ${IOBUFRPC} read "iobufexec2" "stdout" 1 > iobufexec2-read.out &&
    iobuf_cleanup ${pid} &&
    grep "data: foo" iobufexec2-read.out &&
    grep "data_len: 3" iobufexec2-read.out
'

test_expect_success 'iobuf: test eof_count cb after eof on different ranks' '
    pid=$(iobuf_start "iobufexec3" 2 2) &&
    ! grep "eof max reached" service-iobufexec3.out &&
    flux-exec -r 1 ${IOBUFRPC} write "iobufexec3" "stdout" 1 "foo" &&
    flux-exec -r 2 ${IOBUFRPC} write "iobufexec3" "stdout" 2 "foo" &&
    ! grep "eof max reached" service-iobufexec3.out &&
    flux-exec -r 1 ${IOBUFRPC} eof "iobufexec3" "stdout" 1 &&
    flux-exec -r 2 ${IOBUFRPC} eof "iobufexec3" "stdout" 2 &&
    $waitfile --quiet --count=1 --timeout=5 --pattern="eof max reached" service-iobufexec3.out &&
    iobuf_cleanup ${pid}
'

test_done
