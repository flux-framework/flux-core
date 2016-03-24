#!/bin/bash -e

declare top_srcdir=$(pwd)/../..

declare NJOBS=100
declare NNODES=8
declare GITDESC="flux-core $(git describe))"

declare prefix=${TMPDIR-/tmp}/soak-$$

declare LOGFILE=${prefix}.log
declare DATFILE=${prefix}.dat
declare PNGFILE=${prefix}.png

declare existing_data=""
declare sec_max=""
declare gb_max=""

usage() {
    echo "Usage: soak.sh [OPTIONS]" >&2
    echo "  -j NJOBS      set the number of jobs to run (default ${NJOBS})" >&2
    echo "  -n NNODES     run on NNODES (default ${NNODES})" >&2
    echo "  -s SEC_MAX    set range of Y axes (default calculated)" >&2
    echo "  -g GB_MAX     set range of Y2 axes (default calculated)" >&2
    echo "  -d FILE       replot data from FILE - skips running the test" >&2
    echo "If replotting, NJOBS is read from FILE and -n is ignored" >&2
    exit 1
}

die() {
    echo $* >&2
    exit 1
}

while getopts "?hj:n:d:g:s:" opt; do
    case ${opt} in
        h|\?) usage ;;
        j) NJOBS=${OPTARG} ;;
        n) NNODES=${OPTARG} ;;
        d) existing_data=${OPTARG} ;;
        g) gb_max=${OPTARG} ;;
        s) sec_max=${OPTARG} ;;
        *) die "bad option: ${opt}" ;;
    esac
done
shift $((${OPTIND} - 1))
[ $# = 0 ] || usage
[ $NJOBS -gt 0 ] || usage
[ $NNODES -gt 0 ] || usage
if [ -n "$existing_data" ]; then
    [ -r $existing_data ] || usage
    DATFILE=$existing_data
    PNGFILE=$(mktemp).png
    NJOBS=$(tail -1 ${DATFILE} | cut -f1 -d' ')
fi

# Try for consistent Y axes tics across runs
if [ -z "$sec_max" ]; then
    if test ${NJOBS} -le 2000; then
        sec_max=1
    elif test ${NJOBS} -le 10000; then
        sec_max=2
    else
        sec_max=4
    fi
fi

# Try for consistent Y2 axes tics across runs
if [ -z "$gb_max" ]; then
    if test ${NJOBS} -le 2000; then
        gb_max=2
    elif test ${NJOBS} -le 10000; then
        gb_max=4
    else
        gb_max=8
    fi
fi

if [ -z "$existing_data" ]; then
    echo Running ${NJOBS} jobs, logging to ${LOGFILE}
    ${top_srcdir}/src/cmd/flux start -s${NNODES} -o,-S,log-filename=${LOGFILE} \
        ./soak-workload.sh ${NJOBS}

    echo Extracting ${DATFILE} from ${LOGFILE}
    grep soak ${LOGFILE} | sed -e 's/[^:]*: //' >${DATFILE}
fi

echo Plotting to ${PNGFILE}
gnuplot <<EOT
set terminal png font arial 12
set output "${PNGFILE}"

set title "soak test - ${NNODES}-node jobs back to back (${GITDESC})"

set key left top
set xlabel "jobid"
set xrange [0:${NJOBS}]
set ylabel "seconds"
set yrange [0:${sec_max}]
set y2label "gigabytes"
set y2tics
set y2range [0:${gb_max}]
plot \
  "${DATFILE}" using 1:2 with linespoints title 'job runtime',\
  "${DATFILE}" using 1:(\$3/1000000) with linespoints title 'broker rss' axes x1y2,\
  "${DATFILE}" using 1:(\$4/1000000) with linespoints title 'sqlite db' axes x1y2
EOT

echo Displaying ${PNGFILE}
eog ${PNGFILE}

# vi:tabstop=4 shiftwidth=4 expandtab
