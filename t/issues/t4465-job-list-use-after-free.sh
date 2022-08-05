#!/bin/sh -e

VALGRIND=`which valgrind`
if test -z "${VALGRIND}"; then
    exit 0
fi

STATEDIR=issue4470-statedir

rm -rf ${STATEDIR}
mkdir ${STATEDIR}

# This test reproduces the failure more often when jobs complete in
# a different order than submitted, hence number of jobs submitted equals
# number of broker ranks.
#
flux start --test-size=8 -o,-Sstatedir=${STATEDIR} \
    bash -c "flux mini submit --cc 1-8 -q /bin/true && flux queue drain"

flux start --test-size=1 -o,-Sstatedir=${STATEDIR} \
    --wrap=libtool,e,${VALGRIND} \
    --wrap=--tool=memcheck \
    --wrap=--trace-children=no \
    --wrap=--child-silent-after-fork=yes \
    --wrap=--num-callers=30 \
    --wrap=--error-exitcode=1 \
    bash -c "flux job purge --force --num-limit=4 && flux jobs -a 2>/dev/null"
