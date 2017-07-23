
#
#  project-local sharness code for Flux
#

#
#  Extra functions for Flux testsuite
#
run_timeout() {
    perl -e 'alarm shift @ARGV; exec @ARGV' "$@"
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
#  Reinvoke a test file under a flux comms instance
#
#  Usage: test_under_flux <size>
#
test_under_flux() {
    size=${1:-1}
    personality=${2:-full}
    log_file="$TEST_NAME.broker.log"
    if test -n "$TEST_UNDER_FLUX_ACTIVE" ; then
        cleanup rm "${SHARNESS_TEST_DIRECTORY:-..}/$log_file"
        return
    fi
    quiet="-o -q,-Slog-filename=${log_file},-Slog-forward-level=7"
    if test "$verbose" = "t" -o -n "$FLUX_TESTS_DEBUG" ; then
        flags="${flags} --verbose"
        quiet=""
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

    TEST_UNDER_FLUX_ACTIVE=t \
    TERM=${ORIGINAL_TERM} \
      exec flux start --bootstrap=selfpmi --size=${size} ${quiet} ${timeout} \
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
