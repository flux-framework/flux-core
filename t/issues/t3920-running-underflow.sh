#!/bin/sh -e
#
# Event posted in CLEANUP state after an exception in SCHED state
#  does not cause underflow of the running job count.
#
export PLUGIN=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs/cleanup-event.so
flux start '\
   flux jobtap load $PLUGIN \
&& flux queue stop \
&& jobid=$(flux mini submit hostname) \
&& flux job wait-event $jobid depend \
&& flux job cancel $jobid \
&& flux job attach -vE $jobid \
; flux queue status -v >t3920.output 2>&1'

cat t3920.output

grep 'flux-queue: 0 running jobs' t3920.output
