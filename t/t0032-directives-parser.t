#!/bin/sh

test_description='Test python flux.job.directives parser operation'

. `dirname $0`/sharness.sh

parser="flux python -m flux.job.directives"

test_expect_success 'flux.job.directives works on file with no directives' '
	$parser <<-EOF >empty.out &&
	#!/bin/sh
	hostname
	EOF
	test_debug "cat empty.out" &&
	test_must_be_empty empty.out
'
TESTDIR=${SHARNESS_TEST_SRCDIR}/batch/directives
VALID=$TESTDIR/valid
INVALID=$TESTDIR/invalid

validate_directives() {
	input=$(basename $1) &&
	testname=${input%.*} &&
	out=${testname}.output &&
	exp=$(dirname $1)/expected/${testname}.expected &&
	$parser $1 >$out 2>&1 &&
	test_debug "cat $out" &&
	test_cmp $exp $out
}

for file in ${VALID}/*; do
	test -d $file && continue
	input=$(basename $file) &&
	test_expect_success 'valid: '${input} "validate_directives $file"
done

test_invalid_directive() {
	input=$(basename $1) &&
	testname=${input%.*} &&
	out=${testname}.output &&
	errmsg=$(dirname $1)/expected/${testname}.pattern &&
	test_must_fail $parser $1 >$out 2>&1 &&
	test_debug "cat $out" &&
	grep "$(cat $errmsg)" $out
}

for file in ${INVALID}/*; do
	test -d $file && continue
	input=$(basename $file) &&
	test_expect_success 'invalid: '${input} "test_invalid_directive $file"
done

test_done
