#!/bin/bash

echo FLUX_URI=$FLUX_URI
exitcode=0

for file in ${SHARNESS_TEST_SRCDIR:-..}/valgrind/workload.d/*; do
	echo Running $(basename $file)
	$file
	rc=$?
	if test $rc -gt 0; then
		echo "$(basename $file): Failed with rc=$rc" >&2
		exitcode=1
	fi
done
exit $exitcode
