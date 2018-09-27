#!/bin/sh
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
set -ex

ARGS="$@"
JOBS=${JOBS:-2}
MAKECMDS="make -j ${JOBS} ${DISTCHECK:+dist}check"

# Add non-standard path for libfaketime to LD_LIBRARY_PATH:
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu/faketime"

# Force git to update the shallow clone and include tags so git-describe works
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
    MAKECMDS="make -j $JOBS && \
              make -j $JOBS check-code-coverage && \
              lcov -l flux*-coverage.info"

# Use make install for T_INSTALL:
elif test "$TEST_INSTALL" = "t"; then
    ARGS="$ARGS --prefix=/usr --sysconfdir=/etc"
    MAKECMDS="make -j $JOBS && sudo make install && \
              /usr/bin/flux keygen --force && \
              FLUX_TEST_INSTALLED_PATH=/usr/bin make -j $JOBS check"
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

./autogen.sh
./configure ${ARGS}
make clean
eval ${MAKECMDS}
