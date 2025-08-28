#!/bin/sh

test_description='Test broker rundir and statedir behavior'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

# Avoid loading modules where unnecessary to speed up the test
ARGS_NORC="-Sbroker.rc1_path= -Sbroker.rc3_path="

# TMPDIR might interfere with tests
unset TMPDIR

test_expect_success 'create test script' '
	cat >checkattrs.sh <<-EOT &&
	#!/bin/sh -e
	printf "rundir=%s\n" \$(flux getattr rundir)
	printf "statedir=%s\n" \$(flux getattr statedir)
	printf "rundir-cleanup=%s\n" \$(flux getattr rundir-cleanup)
	printf "statedir-cleanup=%s\n" \$(flux getattr statedir-cleanup)
	EOT
	chmod +x checkattrs.sh
'

# Usage: kv_lookup key [file]
# Parse value from key=value file
kv_lookup() {
	grep "^$1=" $2 | sed s/^$1=//
}
# Usage: kv_check key expected-value [file]
kv_check() {
	test $(kv_lookup $1 $3) = $2
}

# Repeat the next block of tests for rundir and statedir since
# they are handled identically.

for attr in rundir statedir; do

    test_expect_success "run test script with defaults" '
	flux start ${ARGS_NORC} ./checkattrs.sh >$attr-defaults.out
    '
    test_expect_success "$attr-cleanup is set to 1" '
	kv_check $attr-cleanup 1 $attr-defaults.out
    '
    test_expect_success "$attr was cleaned up" '
	test ! -d $(kv_lookup $attr $attr-defaults.out)
    '
    test_expect_success "-S$attr-cleanup=0 may be set on the command line" '
	flux start ${ARGS_NORC} -S$attr-cleanup=0 \
		./checkattrs.sh >$attr-cleanup.out &&
	kv_check $attr-cleanup 0 $attr-cleanup.out
    '
    test_expect_success "and $attr was not cleaned up" '
	dirname=$(kv_lookup $attr $attr-cleanup.out) &&
	test -d $dirname &&
	rm -rf $dirname
    '
    test_expect_success "-S$attr may be set on the command line" '
	dirname=`mktemp -d` &&
	flux start ${ARGS_NORC} -S$attr=$dirname \
		./checkattrs.sh >$attr.out &&
	kv_check $attr $dirname $attr.out
    '
    test_expect_success "$attr-cleanup defaults to 0 when $attr is specified" '
	kv_check $attr-cleanup 0 $attr.out
    '
    test_expect_success "and $attr was not cleaned up" '
	dirname=$(kv_lookup $attr $attr.out) &&
	test -d $dirname &&
	rm -rf $dirname
    '
    test_expect_success "-S$attr-cleanup=1 overrides this behavior" '
	dirname=`mktemp -d` &&
	flux start ${ARGS_NORC} -S$attr=$dirname -S$attr-cleanup=1 \
		./checkattrs.sh >$attr-nokeep.out &&
	kv_check $attr $dirname $attr-nokeep.out &&
	kv_check $attr-cleanup 1 $attr-nokeep.out
    '
    test_expect_success "and $attr was cleaned up" '
	dirname=$(kv_lookup $attr $attr-nokeep.out) &&
	test ! -d $dirname
    '
    test_expect_success "-S$attr directory is created if it doesn't exist" '
	dirname=`mktemp -d` &&
	rmdir $dirname &&
	flux start ${ARGS_NORC} -S$attr=$dirname \
		./checkattrs.sh >$attr-noexist.out &&
	kv_check $attr $dirname $attr-noexist.out
    '
    test_expect_success "$attr-cleanup defaults to 1 when $attr is created" '
	kv_check $attr-cleanup 1 $attr-noexist.out
    '
    test_expect_success "$attr was cleaned up" '
	test ! -d $(kv_lookup $attr $attr-noexist.out)
    '
    test_expect_success "-S$attr-cleanup=0 overrides this behavior" '
	dirname=`mktemp -d` &&
	rmdir $dirname &&
	flux start ${ARGS_NORC} -S$attr=$dirname -S$attr-cleanup=0 \
		./checkattrs.sh >$attr-noexist-keep.out &&
	kv_check $attr $dirname $attr-noexist-keep.out &&
	kv_check $attr-cleanup 0 $attr-noexist-keep.out
    '
    test_expect_success "and $attr was not cleaned up" '
	dirname=$(kv_lookup $attr $attr-noexist-keep.out) &&
	test -d $dirname &&
	rm -rf $dirname
    '
    test_expect_success "$attr, $attr-cleanup cannot be updated at runtime" '
	test_must_fail flux start ${ARGS_NORC} flux setattr $attr x &&
	test_must_fail flux start ${ARGS_NORC} flux setattr $attr-cleanup x
    '

done # for loop

test_expect_success 'rundir uses /tmp, while statedir uses /var/tmp' '
	flux start ${ARGS_NORC} ./checkattrs.sh >tmppaths.out &&
	grep "^rundir=/tmp/" tmppaths.out &&
	grep "^statedir=/var/tmp/" tmppaths.out
'
test_expect_success 'but TMPDIR overrides both' '
	dirname=`mktemp -d` &&
	TMPDIR=$dirname flux start ${ARGS_NORC} ./checkattrs.sh >tmpdir.out &&
	grep "^rundir=$dirname/" tmpdir.out &&
	grep "^statedir=$dirname/" tmpdir.out &&
	rmdir $dirname
'
test_expect_success 'broker fails gracefully when rundir buffer overflows' '
	longstring=$(head -c 1024 < /dev/zero | tr \\0 D) &&
	! TMPDIR=$longstring flux start ${ARGS_NORC} true 2>overflow.err &&
	grep overflow overflow.err
'
test_expect_success 'broker fails gracefully on nonexistent TMPDIR' '
	! TMPDIR=/noexist flux start ${ARGS_NORC} true 2>noexist.err &&
	grep "cannot create directory in /noexist" noexist.err
'
test_expect_success 'broker fails gracefully on non-directory rundir' '
	touch notdir &&
	test_must_fail flux start ${ARGS_NORC} -Srundir=notdir \
		true 2>notdir.err &&
	grep "Not a directory" notdir.err
'
test_expect_success 'broker fails gracefully on unwriteable rundir' '
	mkdir -p privdir &&
	chmod u-w privdir &&
	test_must_fail flux start ${ARGS_NORC} -Srundir=privdir \
		true 2>privdir.err &&
	grep "permissions" privdir.err
'

test_done
