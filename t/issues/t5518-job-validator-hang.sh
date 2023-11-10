#!/bin/sh
#
# Run an instance with a very small job-ingest working buffer size
# and ensure the worker does not hang after errors are returned
#
export FLUX_URI_RESOLVE_LOCAL=t

#  Check if we need to start parent job, if so, reexec under flux-start
if test "$VALIDATOR_HANG_TEST_ACTIVE" != "t"; then
    export VALIDATOR_HANG_TEST_ACTIVE=t
    printf "Re-launching test script under flux-start\n"
    exec flux start -s1 $0
fi

id=$(flux alloc -n1 --bg --conf=ingest.buffer-size=8k)
printf "Launched single core alloc job $id\n"

# Submission of more than 1 job should have some failures, but should not
# hang:
flux proxy $id flux submit --cc=1-10 --watch hostname
rc=$?
printf "submission of multiple jobs got rc=$rc\n"
test $rc -ne 0 || exit 1

# Small job to clear errors
flux proxy $id flux run --env=-* --env=PATH hostname

# Another small job should succeed:
flux proxy $id flux run --env=-* --env=PATH hostname || exit 1
printf "submission of single job still works\n"
