#!/bin/bash

size=$(flux getattr size)

contentdir=$(flux getattr persist-directory 2>/dev/null) \
	|| contentdir=$(flux getattr scratch-directory)

njobs=${1:-2000}

echo FLUX_URI=$FLUX_URI

for i in `seq 1 $njobs`; do
    elapsed=$(/usr/bin/time -f "%e" flux wreckrun --ntasks $size /bin/true 2>&1)
    broker_rss=$(flux comms-stats --rusage cmb --parse maxrss)
    content_k=$(du -sk $contentdir | cut -f1)
    flux logger --priority soak.info $i $elapsed $broker_rss $content_k
done
