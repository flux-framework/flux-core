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

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

# We rm -f watch_out to remove any potential race with backgrounding
# of kvs watch process and a previous test's watch_out file.

test_expect_success NO_CHAIN_LINT 'kvs: watch a key'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.foo=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.foo >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs put --json $DIR.foo=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	0
	1
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key that at first doesnt exist'  '
	flux kvs unlink -Rf $DIR &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.foo >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "nil" watch_out &&
        flux kvs put --json $DIR.foo=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	nil
	1
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key that gets removed'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.foo=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.foo >watch_out &
        watchpid=$!
        $waitfile -q -t 5 -p "0" watch_out &&
        flux kvs unlink $DIR.foo &&
        wait $watchpid
	cat >expected <<-EOF &&
	0
	nil
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key that becomes a dir'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.foo=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.foo >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out &&
        flux kvs put --json $DIR.foo.bar.baz=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	0
	======================
	$DIR.foo.bar.
	======================
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a dir'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a.a=0 $DIR.a.b=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "======================" watch_out &&
        flux kvs put --json $DIR.a.a=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	$DIR.a.
	======================
	$DIR.a.
	======================
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a dir that at first doesnt exist'  '
	flux kvs unlink -Rf $DIR &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "nil" watch_out &&
        flux kvs put --json $DIR.a.a=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	nil
	======================
	$DIR.a.
	======================
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a dir that gets removed'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a.a.a=0 $DIR.a.a.b=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.a >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "======================" watch_out &&
        flux kvs unlink -R $DIR.a &&
        wait $watchpid
	cat >expected <<-EOF &&
	$DIR.a.a.
	======================
	nil
	======================
	EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a dir, converted into a key'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a.a.a=0 $DIR.a.a.b=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.a >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "======================" watch_out &&
        flux kvs put --json $DIR.a=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	$DIR.a.a.
	======================
	1
	EOF
        test_cmp watch_out expected
'

# Difference between this test and prior one is we are converting $DIR
# to a key instead of $DIR.a to a key.  Since we are watching $DIR.a,
# prior test should see conversion of a $DIR.a to a key.  This time,
# $DIR.a is no longer valid and we should see 'nil' as a result.
test_expect_success NO_CHAIN_LINT 'kvs: watch a dir, prefix path converted into a key'  '
        flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a.a.a=0 $DIR.a.a.b=0 &&
        rm -f watch_out
	flux kvs watch -o -c 1 $DIR.a >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "======================" watch_out &&
        flux kvs put --json $DIR=1 &&
        wait $watchpid
	cat >expected <<-EOF &&
	$DIR.a.a.
	======================
	nil
	======================
	EOF
        test_cmp watch_out expected
'

# Output of watch_out could be unsorted/out of order compared to
# expected output.  This function will re-order the output in each
# appropriate section (i.e. between ====================== lines)
sort_watch_output() {
        rm -f watch_out_sorted
        rm -f tmp_watch_file
        while read line
        do
            if [ "$line" = "======================" ]
            then
                cat tmp_watch_file | sort >> watch_out_sorted
                echo "======================" >> watch_out_sorted
                rm -f tmp_watch_file
            else
                echo "$line" >> tmp_watch_file
            fi
        done <watch_out
        return 0
}

test_expect_success NO_CHAIN_LINT 'kvs: watch a dir with -R'  '
        flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a.a=0 $DIR.a.b=0 &&
        rm -f watch_out
	flux kvs watch -R -o -c 1 $DIR >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "======================" watch_out &&
        flux kvs put --json $DIR.a.a=1 &&
        wait $watchpid
        sort_watch_output
	cat >expected <<-EOF &&
	$DIR.a.a = 0
	$DIR.a.b = 0
	======================
	$DIR.a.a = 1
	$DIR.a.b = 0
	======================
	EOF
        test_cmp watch_out_sorted expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a dir with -R and -d'  '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a.a=0 $DIR.a.b=0 &&
        rm -f watch_out
	flux kvs watch -R -d -o -c 1 $DIR >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "======================" watch_out &&
        flux kvs put --json $DIR.a.a=1 &&
        wait $watchpid
        sort_watch_output
	cat >expected <<-EOF &&
	$DIR.a.a
	$DIR.a.b
	======================
	$DIR.a.a
	$DIR.a.b
	======================
	EOF
        test_cmp watch_out_sorted expected
'

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
