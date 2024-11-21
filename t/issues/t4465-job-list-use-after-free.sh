#!/bin/sh -e

prog=$(basename $0)

if test -z "$FLUX_ENABLE_VALGRIND_TEST"; then
    echo "skipping ${prog}: FLUX_ENABLE_VALGRIND_TEST is not set" >&2
    exit 0
fi
VALGRIND=`which valgrind`
if test -z "${VALGRIND}"; then
    echo "skipping ${prog}: valgrind executable not found" >&2
    exit 0
fi
VALGRIND_SUPPRESSIONS=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind.supp

STATEDIR=issue4470-statedir

rm -rf ${STATEDIR}
mkdir ${STATEDIR}

# This test reproduces the failure more often when jobs complete in
# a different order than submitted, hence number of jobs submitted equals
# number of broker ranks.
#
flux start --test-size=8 -Sstatedir=${STATEDIR} \
    bash -c "flux submit --cc 1-8 --quiet /bin/true && flux queue drain"

flux start --test-size=1 -Sstatedir=${STATEDIR} \
    --wrap=libtool,e,${VALGRIND} \
    --wrap=--tool=memcheck \
    --wrap=--trace-children=no \
    --wrap=--child-silent-after-fork=yes \
    --wrap=--num-callers=30 \
    --wrap=--error-exitcode=1 \
    --wrap=--suppressions=$VALGRIND_SUPPRESSIONS \
    bash -c "flux job purge --force --num-limit=4 && flux jobs -a 2>/dev/null"
