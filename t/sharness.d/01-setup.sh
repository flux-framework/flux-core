#
#  Export some extra variables to test scripts specific to flux-core
#   testsuite

#
#  Unset variables important to Flux
#
unset FLUX_CONFIG
unset FLUX_MODULE_PATH
unset FLUX_PMI_CLIENT_SEARCHPATH
unset FLUX_PMI_CLIENT_METHODS

# Unset any user defined output defaults, since that may mess up tests
unset FLUX_JOBS_FORMAT_DEFAULT
unset FLUX_RESOURCE_LIST_FORMAT_DEFAULT
unset FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT
unset FLUX_RESOURCE_STATUS_FORMAT_DEFAULT
unset FLUX_QUEUE_LIST_FORMAT_DEFAULT
unset FLUX_PGREP_FORMAT_DEFAULT

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
        FLUX_SOURCE_DIR="$(cd ${SHARNESS_TEST_SRCDIR}/.. && pwd)"
    else
        FLUX_SOURCE_DIR="$(cd ${srcdir}/.. && pwd)"
    fi
    export FLUX_SOURCE_DIR
fi


#
#  Add path to flux(1) command to PATH
#
if test -n "$FLUX_TEST_INSTALLED_PATH"; then
    PATH=$FLUX_TEST_INSTALLED_PATH:$PATH
    fluxbin=$FLUX_TEST_INSTALLED_PATH/flux
else # normal case, use ${top_builddir}/src/cmd/flux
    #
    #  Ensure that the path to the configured Python is first in PATH
    #  so the correct version is found by '#!/usr/bin/env python3' in
    #  several test scripts that use this shebang line.
    #  N.B.: This is not a complete fix. See flux-core #5091 for details
    #
    PATH=$($FLUX_BUILD_DIR/src/cmd/flux python -c \
           'import os,sys; print(os.path.dirname(sys.executable))'):$PATH

    PATH=$FLUX_BUILD_DIR/src/cmd:$PATH
    fluxbin=$FLUX_BUILD_DIR/src/cmd/flux

    #  Ensure that the built libflux-*.so are found before any system
    #   installed versions. This is necessary because sometimes libtool
    #   will use -rpath /usr/lib64 even for uninstalled test programs
    #   (e.g. compiled MPI test programs like t/mpi/version)
    #
    export LD_LIBRARY_PATH="${FLUX_BUILD_DIR}/src/common/.libs:$LD_LIBRARY_PATH"
fi
export PATH

if ! test -x ${fluxbin}; then
    echo >&2 "Failed to find a flux binary in ${fluxbin}."
    echo >&2 "Do you need to run make?"
    return 1
fi

#  flux(1) prepends the correct path(s) to PYTHONPATH when invoking
#   Python subcommands or when `flux env` and `flux python` are invoked.
#   However, this is not always the case for Python based test scripts
#   run under sharness, so these test scripts can end up loading system
#   installed Flux modules and fail in unpredictable ways. Therefore,
#   export the correct PYTHONPATH here:
#
PYTHONPATH="$($FLUX_BUILD_DIR/src/cmd/flux env printenv PYTHONPATH)"

#  Python's site module won't be able to determine the correct path
#   for site.USER_SITE because sharness reassigns HOME to a per-test
#   trash directory. Set up a REAL_HOME here from the passwd database
#   and append the real USER_SITE to PYTHONPATH so Python can find
#   user installed modules (e.g. those installed with pip install --user)
#
REAL_HOME=$(getent passwd $USER | cut -d: -f6)
USER_SITE=$(HOME=$REAL_HOME flux python -c 'import site; print(site.USER_SITE)')
PYTHONPATH=${PYTHONPATH:+${PYTHONPATH}:}${USER_SITE}
export REAL_HOME PYTHONPATH

# vi: ts=4 sw=4 expandtab
