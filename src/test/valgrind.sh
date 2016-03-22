#!/bin/bash

NJOBS=${1-10}
LOGFILE=${TMPDIR-/tmp}/valgrind-$$.log
top_srcdir=$(pwd)/../..

export FLUX_RETRY_COUNT=20

echo Running ${NJOBS} jobs under valgrind, output to ${LOGFILE}
${top_srcdir}/src/cmd/flux /usr/bin/valgrind \
    --log-file=${LOGFILE} \
    --tool=memcheck \
    --leak-check=full \
    --trace-children=no \
    --child-silent-after-fork=yes \
    --num-callers=30 \
    --leak-resolution=med \
    --suppressions=valgrind.supp \
    ${top_srcdir}/src/broker/.libs/lt-flux-broker --boot-method=single \
    ./valgrind-workload.sh ${NJOBS}
