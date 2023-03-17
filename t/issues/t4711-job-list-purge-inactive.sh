#!/usr/bin/env bash
set -e

prejob=$(flux job stats | jq .job_states.run)

jobid=$(flux submit --wait-event=start sleep 100 | flux job id)

postjob=$(flux job stats | jq .job_states.run)

if [ ${postjob} -ne $((prejob + 1)) ]
then
    echo "stat counts invalid: ${postjob}, ${prejob}"
    exit 1
fi

flux event pub job-purge-inactive "{\"jobs\":[${jobid}]}"

# if job-list module asserted this won't work

# this is technically a bit racy, as we won't know for sure if the
# event has been received / processed by the job-list module yet.  In
# the rare event the racy bit is hit and our fix regresses, subsequent
# flux actions should catch a failure.

flux job stats
