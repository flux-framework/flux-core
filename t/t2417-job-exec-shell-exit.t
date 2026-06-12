#!/bin/sh

test_description='Test job-exec shell-exit event and shell-exit-timeout'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"

# Create a wrapper that runs the real flux-shell but then hangs forever
# on non-leader broker ranks after the real shell exits.  The real remote
# shell sends output eof during teardown, which causes the leader to exit
# normally -- but job-exec's subprocess for that rank stays alive.
# Use "exec sleep inf" so there is exactly one process for the kill machinery
# to land on and no orphan is left behind.
test_expect_success 'create stuck job shell wrapper' '
	cat >stuck-shell.sh <<-EOF &&
	#!/bin/sh
	rank=\$(flux getattr rank)
	${FLUX_SHELL} "\$@"
	rc=\$?
	test "\$rank" -ne 0 && exec sleep inf
	exit \$rc
	EOF
	chmod +x stuck-shell.sh
'

# Create a wrapper that delays exit on non-leader ranks by 2 seconds only,
# used to test that the timer is properly stopped on normal completion.
test_expect_success 'create slow-exit job shell wrapper' '
	cat >slow-shell.sh <<-EOF &&
	#!/bin/sh
	rank=\$(flux getattr rank)
	${FLUX_SHELL} "\$@"
	rc=\$?
	test "\$rank" -ne 0 && sleep 2
	exit \$rc
	EOF
	chmod +x slow-shell.sh
'

# ---------------------------------------------------------------------------
# Test 6 (config validation) -- run first so later tests start clean
# ---------------------------------------------------------------------------

test_expect_success 'invalid shell-exit-timeout fails config load' '
	test_expect_code 1 flux config load <<-EOF
	[exec]
	shell-exit-timeout = "notfsd"
	EOF
'
test_expect_success 'valid FSD shell-exit-timeout loads successfully' '
	flux config load <<-EOF &&
	[exec]
	shell-exit-timeout = "1m"
	EOF
	flux module reload job-exec &&
	flux module stats job-exec \
	    | jq -e ".\"bulk-exec\".config.shell_exit_timeout == 60."
'
test_expect_success 'shell-exit-timeout = "none" disables the timer' '
	flux config load <<-EOF &&
	[exec]
	shell-exit-timeout = "none"
	EOF
	flux module reload job-exec &&
	flux module stats job-exec \
	    | jq -e ".\"bulk-exec\".config.shell_exit_timeout == 0."
'
test_expect_success 'reload job-exec to defaults' '
	flux config load </dev/null &&
	flux module reload job-exec
'

# ---------------------------------------------------------------------------
# Test 1: shell-exit event is posted with correct context
# ---------------------------------------------------------------------------

test_expect_success 'reload job-exec with shell-exit-timeout=none' '
	flux module reload job-exec shell-exit-timeout=none
'
test_expect_success 'submit stuck-shell job' '
	id=$(flux submit -N2 -n2 -o exit-timeout=none \
	     --setattr=exec.job_shell=$(pwd)/stuck-shell.sh true) &&
	echo $id >stuck-id.txt
'
test_expect_success 'shell-exit event is posted' '
	id=$(cat stuck-id.txt) &&
	flux job wait-event -p exec -vt 30 $id shell-exit
'
test_expect_success 'shell-exit event has correct wait_status and rank' '
	id=$(cat stuck-id.txt) &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context.wait_status == 0 and .context.rank == 0"
'
test_expect_success 'shell-exit event has active_ranks = "1"' '
	id=$(cat stuck-id.txt) &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context.active_ranks == \"1\""
'
test_expect_success 'cancel stuck job and wait for clean' '
	id=$(cat stuck-id.txt) &&
	flux cancel $id &&
	flux job wait-event -t 30 $id clean
'
test_expect_success 'reload job-exec to defaults' '
	flux module reload job-exec
'

# ---------------------------------------------------------------------------
# Test 2: shell-exit-timeout raises exception and job reaches INACTIVE
# ---------------------------------------------------------------------------

test_expect_success 'reload job-exec with shell-exit-timeout=0.2s' '
	flux module reload job-exec shell-exit-timeout=0.2s
'
test_expect_success 'submit stuck-shell job for timer test' '
	id=$(flux submit -N2 -n2 -o exit-timeout=none \
	     --setattr=exec.job_shell=$(pwd)/stuck-shell.sh true) &&
	echo $id >stuck-timer-id.txt
'
test_expect_success 'timer raises exec exception' '
	id=$(cat stuck-timer-id.txt) &&
	flux job wait-event -t 30 -m type=exec $id exception
'
test_expect_success 'exception note mentions timeout duration' '
	id=$(cat stuck-timer-id.txt) &&
	flux job wait-event -t 10 --format=json -m type=exec $id exception \
	    | jq -e ".context.note | test(\"0.2\")"
'
test_expect_success 'job reaches clean without manual intervention' '
	id=$(cat stuck-timer-id.txt) &&
	flux job wait-event -t 30 $id clean
'
test_expect_success 'reload job-exec to defaults' '
	flux module reload job-exec
'

# ---------------------------------------------------------------------------
# Test 3: timer is stopped on normal completion (no false positive)
# ---------------------------------------------------------------------------

test_expect_success 'reload job-exec with shell-exit-timeout=10s' '
	flux module reload job-exec shell-exit-timeout=10s
'
test_expect_success 'submit slow-shell job (exits 2s after leader)' '
	id=$(flux submit -N2 -n2 -o exit-timeout=none \
	     --setattr=exec.job_shell=$(pwd)/slow-shell.sh true) &&
	echo $id >slow-id.txt
'
test_expect_success 'job completes with exit status 0' '
	id=$(cat slow-id.txt) &&
	flux job wait-event -t 30 $id clean &&
	flux job status $id
'
test_expect_success 'no exception event in eventlog' '
	id=$(cat slow-id.txt) &&
	test_must_fail flux job wait-event -t 2 $id exception
'
test_expect_success 'reload job-exec to defaults' '
	flux module reload job-exec
'

# ---------------------------------------------------------------------------
# Test 4: single-shell and normal jobs unaffected
# ---------------------------------------------------------------------------

test_expect_success 'single-node job gets shell-exit event' '
	id=$(flux submit -N1 true) &&
	flux job wait-event -p exec -t 30 $id shell-exit
'
test_expect_success 'single-node shell-exit has wait_status == 0' '
	id=$(flux job last) &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context.wait_status == 0"
'
test_expect_success 'single-node shell-exit has no active_ranks key' '
	id=$(flux job last) &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context | has(\"active_ranks\") | not"
'

# ---------------------------------------------------------------------------
# Test 5: abnormal leader exit gives nonzero wait_status
# ---------------------------------------------------------------------------

test_expect_success 'submit job with task killed by signal' '
	id=$(flux submit -N1 sh -c "kill -9 \$\$") &&
	flux job wait-event -t 30 $id clean
'
test_expect_success 'shell-exit event has nonzero wait_status' '
	id=$(flux job last) &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context.wait_status != 0"
'

test_done
