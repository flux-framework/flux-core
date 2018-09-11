#!/bin/sh
set -ex

# Skip build if we already ran coverity-scan
test "${TRAVIS_BRANCH}" != 'coverity_scan' || exit 0
# Force git to update the shallow clone and include tags so git-describe works
git fetch --unshallow --tags || true
ulimit -c unlimited
sudo /usr/sbin/update-ccache-symlinks
export PATH=/usr/lib/ccache:$PATH
export MAKECMDS="make distcheck"
# Ensure travis builds libev such that libfaketime will work:
# (force libev to *not* use syscall interface for clock_gettime())
export CPPFLAGS="$CPPFLAGS -DEV_USE_CLOCK_SYSCALL=0 -DEV_USE_MONOTONIC=1"

# Travis has limited resources, even though number of processors might
#  might appear to be large. Limit session size for testing to 5 to avoid
#  spurious timeouts.
export FLUX_TEST_SIZE_MAX=5

# Invoke MPI tests
export TEST_MPI=t

# Enable coverage for $CC-coverage build
# We can't use distcheck here, it doesn't play well with coverage testing:
if test "$COVERAGE" = "t" ; then ARGS="$ARGS --enable-code-coverage"; MAKECMDS="make -j 2 && make -j 2 check-code-coverage && lcov -l flux*-coverage.info"; fi
# Use make install for T_INSTALL:
if test "$T_INSTALL" = "t" ; then ARGS="$ARGS --prefix=/tmp/flux"; MAKECMDS="make && make install && /tmp/flux/bin/flux keygen && FLUX_TEST_INSTALLED_PATH=/tmp/flux/bin make -j 2 check"; fi
# Use src/test/cppcheck.sh instead of make?:
if test "$CPPCHECK" = "t" ; then MAKECMDS="sh -x src/test/cppcheck.sh && make"; fi

export FLUX_TESTS_LOGFILE=t
export DISTCHECK_CONFIGURE_FLAGS=${ARGS}

./autogen.sh
mkdir -p travis-build
cd travis-build
../configure ${ARGS}
eval $MAKECMDS
