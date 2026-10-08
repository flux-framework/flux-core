#!/bin/sh

test_description='Self-test the check-format-args static checker'

. `dirname $0`/sharness.sh

checker=${SHARNESS_TEST_SRCDIR}/../scripts/check-format-args
selftest=${SHARNESS_TEST_SRCDIR}/scripts/test-check-format-args.py

#  The checker needs a clang binary that supports -ast-dump=json.  Probe a
#  range of names (unversioned plus versions shipped by our CI images) and
#  forward the one we find to the self-test via CHECK_FORMAT_ARGS_CLANG.
#
for c in clang clang-19 clang-18 clang-17 clang-16 clang-15; do
	if command -v $c >/dev/null 2>&1; then
		CHECK_FORMAT_ARGS_CLANG=$c
		break
	fi
done
if test -n "$CHECK_FORMAT_ARGS_CLANG"; then
	test_set_prereq HAVE_CLANG
fi

export CHECK_FORMAT_ARGS_CLANG

test_expect_success HAVE_CLANG 'check-format-args self-test passes' '
	${PYTHON:-python3} $selftest
'

test_done
