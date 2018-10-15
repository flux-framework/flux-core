#!/bin/bash
#
#  Test runner script meant to be executed inside of a docker container
#
#  Usage: travis_run.sh [OPTIONS...]
#
#  Where OPTIONS are passed directly to ./configure
#
#  The script is otherwise influenced by the following environment variables:
#
#  JOBS=N        Argument for make's -j option, default=2
#  COVERAGE      Run with --enable-code-coverage, `make check-code-coverage`
#  TEST_INSTALL  Run `make check` against installed flux-core
#  CPPCHECK      Run cppcheck if set to "t"
#  DISTCHECK     Run `make distcheck` if set
#  chain_lint    Run sharness with --chain-lint if chain_lint=t
#
#  And, obviously, some crucial variables that configure itself cares about:
#
#  CC, CXX, LDFLAGS, CFLAGS, etc.
#

# if make is old, and scl is here, and devtoolset is available and not turned
# on, re-exec ourself with it active to get a newer make
if make --version | grep 'GNU Make 4' 2>&1 > /dev/null ; then
  MAKE="make --output-sync=target"
else
  MAKE="make" #use this if all else fails
  if test "X$X_SCLS" = "X" ; then
    if scl -l | grep devtoolset-7 2>&1 >/dev/null ; then
      echo  bash "$0" "$@" | scl enable devtoolset-7 -
      exit
    fi
  fi
fi

# source travis_fold and travis_time functions:
. src/test/travis-lib.sh

ARGS="$@"
JOBS=${JOBS:-2}
MAKECMDS="${MAKE} -j ${JOBS} ${DISTCHECK:+dist}check"

# Add non-standard path for libfaketime to LD_LIBRARY_PATH:
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu/faketime"

# Force git to update the shallow clone and include tags so git-describe works
travis_fold "git_fetch_tags" "git fetch --unshallow --tags" \
 git fetch --unshallow --tags || true
ulimit -c unlimited

# Manually update ccache symlinks (XXX: Is this really necessary?)
test -x /usr/sbin/update-ccache-symlinks && \
    sudo /usr/sbin/update-ccache-symlinks
export PATH=/usr/lib/ccache:$PATH

# Ensure ccache dir exists
mkdir -p $HOME/.ccache

# clang+ccache requries second cpp pass:
if echo "$CC" | grep -q "clang"; then
    CCACHE_CPP=1
fi

# Ensure travis builds libev such that libfaketime will work:
# (force libev to *not* use syscall interface for clock_gettime())
export CPPFLAGS="$CPPFLAGS -DEV_USE_CLOCK_SYSCALL=0 -DEV_USE_MONOTONIC=1"

# Ensure we always use internal <flux/core.h> by placing a dummy file
#  in the same path as ZMQ_FLAGS:
sudo sh -c "mkdir -p /usr/include/flux \
    && echo '#error Non-build-tree flux/core.h!' > /usr/include/flux/core.h"

# Enable coverage for $CC-coverage build
# We can't use distcheck here, it doesn't play well with coverage testing:
if test "$COVERAGE" = "t"; then
    ARGS="$ARGS --enable-code-coverage"
    MAKECMDS="${MAKE} -j $JOBS && \
              ${MAKE} -j $JOBS check-code-coverage && \
              lcov -l flux*-coverage.info"

# Use make install for T_INSTALL:
elif test "$TEST_INSTALL" = "t"; then
    ARGS="$ARGS --prefix=/usr --sysconfdir=/etc"
    MAKECMDS="${MAKE} -j $JOBS && sudo make install && \
              /usr/bin/flux keygen --force && \
              FLUX_TEST_INSTALLED_PATH=/usr/bin ${MAKE} -j $JOBS check"
fi

# Travis has limited resources, even though number of processors might
#  might appear to be large. Limit session size for testing to 5 to avoid
#  spurious timeouts.
export FLUX_TEST_SIZE_MAX=5

# Invoke MPI tests
export TEST_MPI=t

# Generate logfiles from sharness tests for extra information:
export FLUX_TESTS_LOGFILE=t
export DISTCHECK_CONFIGURE_FLAGS="${ARGS}"

if test "$CPPCHECK" = "t"; then
    sh -x src/test/cppcheck.sh
fi

travis_fold "autogen.sh" "./autogen.sh..." ./autogen.sh
travis_fold "configure"  "./configure ${ARGS}..." ./configure ${ARGS}
travis_fold "make_clean" "make clean..." make clean

eval ${MAKECMDS}
