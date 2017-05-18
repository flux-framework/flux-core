#!/bin/bash

size=$(flux getattr size)

contentdir=$(flux getattr persist-directory 2>/dev/null) \
	|| contentdir=$(flux getattr broker.rundir)

declare heap_prefix=""
declare njobs=10

while getopts "j:H:" opt; do
    case ${opt} in
        j) njobs=${OPTARG} ;;
        H) heap_prefix=${OPTARG} ;;
    esac
done

echo FLUX_URI=$FLUX_URI

if [ -n "${heap_prefix}" ]; then
    flux heaptrace start ${heap_prefix}
    echo Enabled heap tracing of rank 0 broker to ${heap_prefix}.NNNN.heap
fi

for i in `seq 1 $njobs`; do
    elapsed=$(/usr/bin/time -f "%e" flux wreckrun --ntasks $size /bin/true 2>&1)
    broker_rss=$(flux module stats --parse maxrss --rusage cmb)
    content_k=$(du -sk $contentdir | cut -f1)
    flux logger --severity=info --appname=soak $i $elapsed $broker_rss $content_k
    if [ -n "${heap_prefix}" ]; then
        flux heaptrace dump lwj.$i
    fi
done

if [ -n "${heap_prefix}" ]; then
    flux heaptrace stop
    echo Stopped heap tracing
fi
