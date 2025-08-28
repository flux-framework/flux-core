#!/bin/sh
# ci=system

test_description='Run tests against a system instance of Flux'

#  If FLUX_TEST_INSTALLED_PATH is not set and /usr/bin/flux exists,
#  set FLUX_TEST_INSTALLED_PATH to /usr/bin.
#
#  Must set FLUX_TEST_INSTALLED_PATH before sourcing sharness,
#  otherwise correct flux may not be used in tests.
if test -n "$FLUX_ENABLE_SYSTEM_TESTS"; then
	if test -x ${FLUX_TEST_INSTALLED_PATH:-/usr/bin}/flux; then
		FLUX_TEST_INSTALLED_PATH=${FLUX_TEST_INSTALLED_PATH:-/usr/bin}
	fi
fi
# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

#  Do not run system tests by default unless FLUX_ENABLE_SYSTEM_TESTS
#   is set in environment (e.g. by CI), or the test run run with -d, --debug
#
if test -z "$FLUX_ENABLE_SYSTEM_TESTS"; then
	skip_all='skipping system tests since FLUX_ENABLE_SYSTEM_TESTS not set'
	test_done
fi

owner=$(flux getattr security.owner 2>/dev/null)
if test -n "$owner" -a "$owner" != "$(id -u)"; then
	test_set_prereq SYSTEM
fi
if ! test_have_prereq SYSTEM; then
	skip_all='skipping system instance tests, no system instance found'
	test_done
fi

if test -z "$T9000_SYSTEM_GLOB"; then
	T9000_SYSTEM_GLOB="*"
fi

#
#  Wrap test_expect_success so we can prepend a custom test-label,
#   then shadow function with an alias. Note that the alias is
#   _not_ called in expect_success_wrap() because POSIX says:
#
#   "To prevent infinite loops in recursive aliasing, if the shell is
#    not currently processing an alias of the same name, the word shall
#    be replaced by the value of the alias; otherwise, it shall not be
#    replaced."
#
#   Sec 2.3.1
#   https://pubs.opengroup.org/onlinepubs/007904975/utilities/xcu_chap02.html
#
expect_success_wrap() {
	if test $# -eq 3; then
		test_expect_success "$1" "$TEST_LABEL: $2" "$3"
	else
		test_expect_success "$TEST_LABEL: $1" "$2"
	fi
}
alias test_expect_success='expect_success_wrap'

#
#  All system instance tests are defined in t/system/*
#  We run them serially to avoid conflicted requests for resources
#   to the enclosing system instance, which in CI may be limited.
#
for testscript in ${FLUX_SOURCE_DIR}/t/system/${T9000_SYSTEM_GLOB}; do
	TEST_LABEL="$(basename $testscript)"
	. $testscript
        # Do final_cleanup between tests
	test_eval_ "$final_cleanup"
	final_cleanup=
done

test_done
