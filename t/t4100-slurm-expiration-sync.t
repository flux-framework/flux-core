#!/bin/sh

test_description='Test Slurm expiration synchronization'

. $(dirname $0)/sharness.sh

# Capture original PATH before test_under_flux re-executes this script
# with a modified PATH, and before we prepend the scripts directory.
ORIG_PATH=$PATH
export ORIG_PATH

test_under_flux 1

# Skip the "squeue not in PATH" test if a real squeue is found in the
# original PATH, since it would be found even without the mock.
! command -v squeue >/dev/null 2>&1 && test_set_prereq NO_SQUEUE

# Put mock squeue script first on PATH
PATH=${SHARNESS_TEST_SRCDIR}/scripts:$PATH
export PATH

# future_time N - return a Unix timestamp N seconds from now
future_time() {
	echo $(( $(date +%s) + $1 ))
}

# check_expiration FILE
# Check that execution.expiration in FILE is within 1 second of
# EXPIRATION_FILE
check_expiration() {
	local expected=$(cat $EXPIRATION_FILE)
	test_debug "echo checking: $(jq .execution.expiration <$1) = $expected"
	cat $1 | jq -e ".execution.expiration - $expected | fabs | . < 1"
}

# check_expiration_zero FILE
# Check that execution.expiration in FILE is 0 (unlimited)
check_expiration_zero() {
	jq -e '.execution.expiration == 0' "$1"
}

EXPIRATION_FILE=expiration.txt

cat >wait-expiration.py <<EOF
import sys
import flux
from flux.resource import ResourceJournalConsumer
from flux.eventlog import EventLogFormatter

expected = float(sys.argv[1])
fh = flux.Flux()

evf = EventLogFormatter(format="text", timestamp_format="human")

consumer = ResourceJournalConsumer(fh).start()
while True:
    event = consumer.poll(15.)
    print(evf.format(event), file=sys.stderr)
    if event.name == "resource-update":
        if abs(event.context.get("expiration", 0.0) - expected) < 1.0:
            break
EOF

#
# flux slurm-expiration-sync failure modes
#
test_expect_success 'flux slurm-expiration-sync fails without --jobid' '
	test_must_fail flux slurm-expiration-sync 2>noslurmid.err
'
test_expect_success 'flux slurm-expiration-sync with invalid jobid fails' '
	test_must_fail flux slurm-expiration-sync --jobid=foo 2>invalidid.err &&
	grep -i "invalid" invalidid.err
'
test_expect_success NO_SQUEUE 'flux slurm-expiration-sync fails if squeue not in PATH' '
	test_must_fail env PATH=$ORIG_PATH \
		flux slurm-expiration-sync --jobid=1234 2>nosqueue.err &&
	grep "squeue not found" nosqueue.err
'
# Note: In following test FLUX_MOCK_SLURM_EXPIRATION_FILE not set,
# so squeue wrapper will fail.
test_expect_success 'flux slurm-expiration-sync fails if squeue fails' '
	test_must_fail flux slurm-expiration-sync --jobid=4 2>squeue-fail.err &&
	grep "squeue failed" squeue-fail.err
'
test_expect_success 'flux slurm-expiration-sync works with unlimited' '
	echo -1 > $EXPIRATION_FILE &&
	FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	    flux slurm-expiration-sync --jobid=1234 2>unlimited.err &&
	test_debug "cat unlimited.err" &&
	grep -i "exiting" unlimited.err
'
#
# Full flux start tests with SLURM_JOB_ID set and mock squeue
#
test_expect_success 'flux start without SLURM_JOB_ID leaves expiration as 0' '
	flux start flux resource R > R-noslurm.json &&
	check_expiration_zero R-noslurm.json
'
test_expect_success 'flux start with invalid SLURM_JOB_ID works with warning' '
	SLURM_JOB_ID=invalid \
	  flux start flux resource R 2>start-invalidid.err &&
	test_debug "cat start-invalidid.err" &&
	grep "invalid SLURM_JOB_ID" start-invalidid.err
'
test_expect_success 'flux start with unlimited Slurm time limit' '
	echo -1 > $EXPIRATION_FILE &&
	SLURM_JOB_ID=1234 \
	  FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	  flux start flux resource R > R-unlimited.json &&
	check_expiration_zero R-unlimited.json
'
test_expect_success 'flux start with 1 hour Slurm time limit' '
	future_time 3600 > $EXPIRATION_FILE &&
	test_debug "cat $EXPIRATION_FILE" &&
	SLURM_JOB_ID=1234 \
	  FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	  flux start flux resource R > R-1h.json &&
	test_debug "cat R-1h.json" &&
	check_expiration R-1h.json
'
test_expect_success 'flux start with 2 hour Slurm time limit' '
	future_time 7200 > $EXPIRATION_FILE &&
	SLURM_JOB_ID=1234 \
	  FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	  flux start flux resource R > R-2h.json &&
	check_expiration R-2h.json
'
test_expect_success 'Slurm time limit is propagated to jobs' '
	future_time 3600 > $EXPIRATION_FILE &&
	SLURM_JOB_ID=1234 \
	  FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	  flux start flux run flux job timeleft > timeleft.out &&
	timeleft=$(cat timeleft.out) &&
	test "$timeleft" -gt 3500 &&
	test "$timeleft" -le 3600
'
test_expect_success 'initial program has YOGRT_BACKEND=flux' '
	future_time 3600 > $EXPIRATION_FILE &&
	SLURM_JOB_ID=1234 \
	  FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	  flux start env > yogrt.env &&
	grep YOGRT_BACKEND=flux yogrt.env
'
#
# Polling test: verify that expiration is updated when Slurm time limit changes
#
test_expect_success 'start slurm-expiration-sync in background with short poll interval' '
	future_time 3600 > $EXPIRATION_FILE &&
	SLURM_JOB_ID=1234 \
	FLUX_SLURM_MOCK_EXPIRATION_FILE=$EXPIRATION_FILE \
	flux exec --bg --label=slurm-sync --waitable \
		flux slurm-expiration-sync -v --jobid=1234 --poll-interval=0.1
'
test_expect_success 'initial expiration is ~1 hour' '
	flux python wait-expiration.py $(cat $EXPIRATION_FILE) &&
	flux resource R > R-poll-initial.json &&
	check_expiration R-poll-initial.json
'
test_expect_success 'update expiration file to ~2 hours and wait for poll' '
	future_time 7200 > $EXPIRATION_FILE &&
	flux python wait-expiration.py $(cat $EXPIRATION_FILE) &&
	flux resource R > R-poll-updated.json &&
	check_expiration R-poll-updated.json
'
# send SIGINT for graceful exit of bg slurm-expiration-sync process:
test_expect_success 'stop slurm-expiration-sync' '
	flux sproc kill --wait 2 slurm-sync
'

test_done
