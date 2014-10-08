
#
#  project-local sharness code for Flux
#

#
#  Unset variables important to Flux
#
unset FLUX_CONFIG
unset FLUX_MODULE_PATH
unset FLUX_CMBD_PATH
unset LUA_PATH
unset LUA_CPATH

#
#  FLUX_BUILD_DIR and FLUX_SOURCE_DIR are set to build and source paths
#  (based on current directory)
#
if test -z "$FLUX_BUILD_DIR"; then
    if test -z "${builddir}"; then
        FLUX_BUILD_DIR="$(cd .. && pwd)"
    else
        FLUX_BUILD_DIR="$(cd ${builddir}/.. && pwd))"
    fi
    export FLUX_BUILD_DIR
fi
if test -z "$FLUX_SOURCE_DIR"; then
    if test -z "${srcdir}"; then
        FLUX_SOURCE_DIR="$(cd .. && pwd)"
    else
        FLUX_SOURCE_DIR="$(cd ${srcdir}/.. && pwd)"
    fi
    export FLUX_SOURCE_DIR
fi


#
#  Extra functions for Flux testsuite
#
run_timeout() {
    perl -e 'alarm shift @ARGV; exec @ARGV' "$@"
}

#
#  Reinvoke a test file under a flux comms instance
#
#  Usage: test_under_flux <size>
#
test_under_flux() {
    size=${1:-1}
    if test -n "$TEST_UNDER_FLUX_ACTIVE" ; then
        unset TEST_UNDER_FLUX_ACTIVE
        return
    fi
    quiet="-o -q"
    if test "$verbose" = "t"; then
        flags="${flags} --verbose"
        quiet=""
    fi
    if test "$debug" = "t"; then
        flags="${flags} --debug"
    fi
    cd $SHARNESS_TEST_DIRECTORY

    TEST_UNDER_FLUX_ACTIVE=t \
    TERM=${ORIGINAL_TERM} \
      exec flux start --size=${size} ${quiet} "sh $0 ${flags}"
}


#
#  Export some extra variables to test scripts specific to Flux
#   testsuite
#
#  Add path to flux(1) command to PATH
#
if test -n "$FLUX_TEST_INSTALLED_PATH"; then
    PATH=$FLUX_TEST_INSTALLED_PATH:$PATH
    fluxbin=$FLUX_TEST_INSTALLED_PATH/flux
else # normal case, use ${top_builddir}/src/cmd/flux
    PATH=$FLUX_BUILD_DIR/src/cmd:$PATH
    fluxbin=$FLUX_BUILD_DIR/src/cmd/flux
fi
export PATH

if ! test -x ${fluxbin}; then
    echo >&2 "Failed to find a flux binary in ${fluxbin}."
    echo >&2 "Do you need to run make?"
    return 1
fi

#  Export a shorter name for this test
TEST_NAME=$SHARNESS_TEST_NAME
export TEST_NAME


# vi: ts=4 sw=4 expandtab
