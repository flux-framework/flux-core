#!/bin/sh
#

test_description='Test wreck resource calculator (rcalc) functionality'

. `dirname $0`/sharness.sh

INPUTDIR=$SHARNESS_TEST_SRCDIR/wreck/input
OUTPUTDIR=$SHARNESS_TEST_SRCDIR/wreck/output
rcalc=${SHARNESS_TEST_DIRECTORY}/wreck/rcalc

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

test_done
