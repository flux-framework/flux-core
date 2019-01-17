#!/bin/bash

echo FLUX_URI=$FLUX_URI

for file in ${SHARNESS_TEST_SRCDIR:-..}/valgrind/workload.d/*; do
	echo Running $(basename $file)
	$file
done
