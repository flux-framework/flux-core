#!/bin/sh
#

test_description='Test shell resource calculator (rcalc) functionality'

. `dirname $0`/sharness.sh

INPUTDIR=$SHARNESS_TEST_SRCDIR/shell/input
OUTPUTDIR=$SHARNESS_TEST_SRCDIR/shell/output
rcalc=${SHARNESS_TEST_DIRECTORY}/shell/rcalc

test_expect_success 'rcalc test utility is built' '
    test -x ${rcalc}
'

for file in ${OUTPUTDIR}/*.expected; do
    base=$(basename $file .expected)
    count=$(echo $base | sed 's/.*\.//')
    base=$(basename $base .${count})
    input=${INPUTDIR}/${base}.json
    output=${base}.${count}.out
    err=${base}.${count}.err
    test_expect_success NO_CHAIN_LINT "$(head -1 $file)" '
        ${rcalc} $count <$input >$output 2>$err || : &&
        test_cmp $file $output &&
        test_cmp $OUTPUTDIR/$err $err
    '
done

for file in ${OUTPUTDIR}/per-resource/*.expected; do
    base=$(basename $file .expected)
    count=$(echo $base | sed 's/.*\.//')
    base=$(basename $base .${count})
    type=$(echo $base | sed 's/.*\.//')
    base=$(basename $base .${type})
    input=${INPUTDIR}/${base}.json
    output=${base}.${type}.${count}.out
    err=${base}.${type}.${count}.err
     test_expect_success NO_CHAIN_LINT "$(head -1 $file)" '
        ${rcalc} -R $type $count <$input >$output 2>$err || true &&
        test_cmp $file $output &&
        test_cmp $OUTPUTDIR/per-resource/$err $err
    '
done

#  More specific rcalc tests
test_expect_success 'rcalc: distribute 5 slots of size 5 across 6 allocated' '
	flux R encode -r 0-1 -c 0-14 | \
	    ${rcalc} --cores-per-slot=5 5 > 55.out &&
	cat >55.expected <<-EOF &&
	Distributing 5 tasks across 2 nodes with 30 cores
	Used 2 nodes
	0: rank=0 ntasks=3 cores=0-14
	1: rank=1 ntasks=2 cores=0-14
	EOF
	test_cmp 55.expected 55.out
'
test_expect_success 'rcalc: distribute can oversubscribe (e.g. for --add-brokers)' '
	flux R encode -r 0-1 -c 0-7 | \
	    ${rcalc} --cores-per-slot=8 3 > 83.out &&
	cat >83.expected <<-EOF &&
	Distributing 3 tasks across 2 nodes with 16 cores
	Used 2 nodes
	0: rank=0 ntasks=2 cores=0-7
	1: rank=1 ntasks=1 cores=0-7
	EOF
	test_cmp 83.expected 83.out
'
test_done
