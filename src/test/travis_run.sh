#!/bin/bash
set -ex

env

# Skip build if we already ran coverity-scan
test "${TRAVIS_BRANCH}" != 'coverity_scan' || exit 0
# Force git to update the shallow clone and include tags so git-describe works
git fetch --unshallow --tags || true
ulimit -c unlimited


# make ccache safe for PRs and clang
test "$TRAVIS_PULL_REQUEST" == "false" || export CCACHE_READONLY=1
if test "$CC" = "clang"; then
  export CCACHE_CPP2=1
fi

sudo /usr/sbin/update-ccache-symlinks
export PATH=/usr/lib/ccache:$PATH

# Ensure travis builds libev such that libfaketime will work:
# (force libev to *not* use syscall interface for clock_gettime())
export CPPFLAGS="$CPPFLAGS -DEV_USE_CLOCK_SYSCALL=0 -DEV_USE_MONOTONIC=1"

# Travis has limited resources, even though number of processors might
#  might appear to be large. Limit session size for testing to 5 to avoid
#  spurious timeouts.
export FLUX_TEST_SIZE_MAX=5

# Invoke MPI tests
export TEST_MPI=t

export FLUX_TESTS_LOGFILE=t
export DISTCHECK_CONFIGURE_FLAGS=${ARGS}

# Ensure we always use internal <flux/core.h> by placing a dummy file
#  in the same path as ZMQ_FLAGS:
sudo mkdir -p /usr/include/flux /usr/local/include/flux
sudo sh -c "echo '#error Non-build-tree flux/core.h!' > /usr/include/flux/core.h"
sudo sh -c "echo '#error Non-build-tree flux/core.h!' > /usr/local/include/flux/core.h"

# set up build directory and autogen
./autogen.sh
mkdir -p travis-build
cd travis-build
CONF=../configure $ARGS

if test "$COVERAGE" = "t" ; then
  # Enable coverage for $CC-coverage build
  # We can't use distcheck here, it doesn't play well with coverage testing:
  # coveralls-lcov required only for coveralls upload:
  sudo gem install coveralls-lcov
  $CONF --enable-code-coverage

  make -j 2
  make -j 2 check-code-coverage
  lcov -l flux*-coverage.info
  # Upload results to coveralls.io for first job of N
  coveralls-lcov flux*-coverage.info
  bash <(curl -s https://codecov.io/bash)
elif test "$T_INSTALL" = "t" ; then
  # Use make install for T_INSTALL:
  $CONF --prefix=/tmp/flux
  make
  make install
  /tmp/flux/bin/flux keygen
  FLUX_TEST_INSTALLED_PATH=/tmp/flux/bin make -j 2 check
elif test "$CPPCHECK" = "t" ; then
  $CONF
  # Use src/test/cppcheck.sh instead of make?:
  sh -x src/test/cppcheck.sh && make
else
  $CONF
  make distcheck
fi
