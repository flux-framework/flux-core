
#
#  project-local sharness code for Flux
#

#
#  Extra functions for Flux testsuite
#
run_timeout() {
    perl -e 'use Time::HiRes qw( ualarm ) ; ualarm ((shift @ARGV) * 1000000) ; exec @ARGV' "$@"
}

#
#  Echo on stdout a reasonable size for a large test session,
#   controllable test-wide via env vars FLUX_TEST_SIZE_MIN and
#   FLUX_TEST_SIZE_MAX.
#
test_size_large() {
    min=${FLUX_TEST_SIZE_MIN:-4}
    max=${FLUX_TEST_SIZE_MAX:-17}
    size=$(($(nproc)+1))
    test ${size} -lt ${min} && size=$min
    test ${size} -gt ${max} && size=$max
    echo ${size}
}


#
#  Tests using test_under_flux() and which load their own modules should
#   ensure those modules are unloaded at the end of the test for proper
#   cleanup and test coverage (also as a general principle, module unload
#   should be something explicitly tested).
#
#  The functions below ensure that every module loaded at "test_done" time
#   when test_under_flux() was used was also loaded before test_under_flux
#   was called.
#
flux_module_list() {
    flux module list | awk '!/^Module/{print $1}' | sort
}

check_module_list() {
    flux_module_list > module-list.final
    while read module; do
       grep "^$module$" module-list.initial >/dev/null 2>&1 \
            || bad="${bad}${bad:+ }$module"
    done < module-list.final
    if test -n "$bad"; then
        test -n "$logfile" \
            && say_color error >&3 \
                 "Error: manually loaded module(s) not unloaded: $bad"
        # This function is run under test_eval_ so redirect
        #  error message to &5 (saved stdout) so text doesn't disappear:
        error >&5 2>&1 "manually loaded module(s) not unloaded: $bad"
    fi
}

#
#  Reinvoke a test file under a flux comms instance
#
#  Usage: test_under_flux <size>
#
test_under_flux() {
    size=${1:-1}
    personality=${2:-full}
    log_file="$TEST_NAME.broker.log"
    if test -n "$TEST_UNDER_FLUX_ACTIVE" ; then
        test "$debug" = "t" || cleanup rm "${SHARNESS_TEST_DIRECTORY:-..}/$log_file"
        flux_module_list > module-list.initial
        cleanup check_module_list
        return
    fi
    if test "$verbose" = "t" -o -n "$FLUX_TESTS_DEBUG" ; then
        flags="${flags} --verbose"
    fi
    if test "$debug" = "t" -o -n "$FLUX_TESTS_DEBUG" ; then
        flags="${flags} --debug"
    fi
    if test "$chain_lint" = "t"; then
        flags="${flags} --chain-lint"
    fi
    if test -n "$logfile" -o -n "$FLUX_TESTS_LOGFILE" ; then
        flags="${flags} --logfile"
    fi
    if test -n "$SHARNESS_TEST_DIRECTORY"; then
        cd $SHARNESS_TEST_DIRECTORY
    fi
    timeout="-o -Sinit.rc2_timeout=300"
    if test -n "$FLUX_TEST_DISABLE_TIMEOUT"; then
        timeout=""
    fi

    if test "$personality" = "minimal"; then
        export FLUX_RC1_PATH=""
        export FLUX_RC3_PATH=""
    elif test "$personality" != "full"; then
        export FLUX_RC1_PATH=$FLUX_SOURCE_DIR/t/rc/rc1-$personality
        export FLUX_RC3_PATH=$FLUX_SOURCE_DIR/t/rc/rc3-$personality
        test -x $FLUX_RC1_PATH || error "cannot execute $FLUX_RC1_PATH"
        test -x $FLUX_RC3_PATH || error "cannot execute $FLUX_RC3_PATH"
    else
        unset FLUX_RC1_PATH
        unset FLUX_RC3_PATH
    fi
    if test -n "$FLUX_TEST_VALGRIND" ; then
        VALGRIND_SUPPRESSIONS=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind.supp
        valgrind="--wrap=libtool,e"
        valgrind="$valgrind,valgrind,--leak-check=full"
        valgrind="$valgrind,--trace-children=no,--child-silent-after-fork=yes"
        valgrind="$valgrind,--leak-resolution=med,--error-exitcode=1"
        valgrind="$valgrind,--suppressions=${VALGRIND_SUPPRESSIONS}"
    fi

    logopts="-o -Slog-filename=${log_file},-Slog-forward-level=7"
    TEST_UNDER_FLUX_ACTIVE=t \
    TERM=${ORIGINAL_TERM} \
      exec flux start --bootstrap=selfpmi --size=${size} \
                      ${logopts} \
                      ${timeout} \
                      ${valgrind} \
                     "sh $0 ${flags}"
}

mock_bootstrap_instance() {
    if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
        unset FLUX_URI
    fi
}

#
#  Execute arguments $2-N on rank or ranks specified in arg $1
#   using the flux-exec utility
#
test_on_rank() {
    test "$#" -ge 2 ||
        error "test_on_rank expects at least two parameters"
    test -n "$TEST_UNDER_FLUX_ACTIVE"  ||
        error "test_on_rank: test_under_flux not active ($TEST_UNDER_FLUX_ACTIVE)"

    ranks=$1; shift;
    flux exec --rank=${ranks} "$@"
}

#  Export a shorter name for this test
TEST_NAME=$SHARNESS_TEST_NAME
export TEST_NAME

#  Test requirements for testsuite
if ! lua -e 'require "posix"'; then
    error "failed to find lua posix module in path"
fi

#  Some tests in flux don't work with --chain-lint, add a prereq for
#   --no-chain-lint:
test "$chain_lint" = "t" || test_set_prereq NO_CHAIN_LINT
# vi: ts=4 sw=4 expandtab
