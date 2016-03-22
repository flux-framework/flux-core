#!/bin/bash -e

# Usage: soak.sh [NJOBS] [NNODES]

LOGFILE=${TMPDIR-/tmp}/soak-$$.log
DATFILE=${TMPDIR-/tmp}/soak-$$.dat
PNGFILE=${TMPDIR-/tmp}/soak-$$.png
NJOBS=${1-100}
NNODES=${2-8}

if test ${NJOBS} -lt 1000; then
    YMAX=1
    Y2MAX=1
elif test ${NJOBS} -lt 10000; then
    YMAX=2
    Y2MAX=8
elif test ${NJOBS} -lt 20000; then
    YMAX=4
    Y2MAX=16
fi

top_srcdir=$(pwd)/../..

echo Running ${NJOBS} jobs, logging to ${LOGFILE}
${top_srcdir}/src/cmd/flux start -s${NNODES} -o,-S,log-filename=${LOGFILE} ./soak-workload.sh ${NJOBS}

echo Extracting ${DATFILE} from ${LOGFILE}
grep soak ${LOGFILE} | sed -e 's/[^:]*: //' >${DATFILE}

echo Plotting to ${PNGFILE}
gnuplot <<EOT
set terminal png font arial 12
set output "${PNGFILE}"

set title "soak test - ${NNODES}-node jobs back to back (flux-core $(git describe))"

set key left top
set xlabel "jobid"
set xrange [0:${NJOBS}]
set ylabel "seconds"
set yrange [0:${YMAX}]
set y2label "gigabytes"
set y2tics
set y2range [0:${Y2MAX}]
plot \
  "${DATFILE}" using 1:2 with linespoints title 'job runtime',\
  "${DATFILE}" using 1:(\$3/1000000) with linespoints title 'broker rss' axes x1y2,\
  "${DATFILE}" using 1:(\$4/1000000) with linespoints title 'sqlite db' axes x1y2
EOT

echo Displaying ${PNGFILE}
eog ${PNGFILE}
