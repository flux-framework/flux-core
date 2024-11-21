#!/bin/sh -e
#
#  Trigger an EHOSTUNREACH error when attempting to kill a job.
#
#  1. Submit job and ensure it has started
#  2. Send SIGSTOP to a leaf broker
#  3. Cancel job and wait for exception to appear in job eventlog
#  4. The subprocess kill RPC should now be sent to the stopped broker
#  5. Kill the stopped broker with SIGKILL
#  6. Wait for job clean event and dump logs
#  7. Ensure killed rank appears in logs for exec_kill message
#
#
export startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"
SHELL=/bin/sh flux start -s 4 -Stbon.topo=kary:4 --test-exit-mode=leader '\
   id=$(flux submit -n4 -N4 sleep 300) \
&& flux job wait-event $id start \
&& $startctl kill 3 19 \
&& flux cancel $id \
&& flux job wait-event $id exception \
&& $startctl kill 3 9 \
&& flux job attach -vE $id \
;  flux dmesg >t3960.output 2>&1'

cat t3960.output

grep 'exec_kill.*rank 3' t3960.output
