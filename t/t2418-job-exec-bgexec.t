#!/bin/sh

test_description='Test job-exec bgexec implementation'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"

# ---------------------------------------------------------------------------
# Config: exec.method selection
# ---------------------------------------------------------------------------

test_expect_success 'default exec.method is bulk-exec' '
	flux module reload job-exec &&
	flux module stats job-exec \
	    | jq -e ".method == \"bulk-exec\""
'
test_expect_success 'invalid exec.method fails config load' '
	test_expect_code 1 flux config load <<-EOF
	[exec]
	method = "notreal"
	EOF
'
test_expect_success 'exec.method=bgexec loads successfully' '
	flux config load <<-EOF &&
	[exec]
	method = "bgexec"
	EOF
	flux module reload job-exec &&
	flux module stats job-exec \
	    | jq -e ".method == \"bgexec\""
'
test_expect_success 'reload job-exec with method=bgexec via cmdline' '
	flux config load </dev/null &&
	flux module reload job-exec method=bgexec &&
	flux module stats job-exec \
	    | jq -e ".method == \"bgexec\""
'
# The sdexec service is not yet supported with method=bgexec.  The
# combination must fail the job cleanly at init rather than crash job-exec
# (see PR #7767 review).  Restore the default config afterward so later
# tests run under rexec.
test_expect_success 'method=bgexec with exec.service=sdexec fails job cleanly' '
	flux config load <<-EOF &&
	[exec]
	method = "bgexec"
	service = "sdexec"
	EOF
	flux module reload job-exec &&
	test_when_finished "flux config load </dev/null && \
	    flux module reload job-exec method=bgexec" &&
	id=$(flux submit -N1 true) &&
	flux job wait-event -t 30 $id clean &&
	test_must_fail flux job status $id &&
	flux job wait-event -Ht 30 $id exception \
	    | grep "failed to initialize implementation"
'

# ---------------------------------------------------------------------------
# Normal execution under bgexec
# ---------------------------------------------------------------------------

test_expect_success 'single-node job runs to completion under bgexec' '
	id=$(flux submit -N1 true) &&
	flux job wait-event -t 30 $id clean &&
	flux job status $id
'
test_expect_success 'multi-node job runs to completion under bgexec' '
	id=$(flux submit -N2 -n2 true) &&
	flux job wait-event -t 30 $id clean &&
	flux job status $id
'
test_expect_success 'job exit status is propagated' '
	test_expect_code 1 flux run -N1 false
'
test_expect_success 'job stdout is captured' '
	flux run -N1 echo hello >bgexec-stdout.out &&
	grep hello bgexec-stdout.out
'
test_expect_success 'shell stderr is captured to exec.eventlog' '
	cat >stderr.lua <<-EOT &&
	for i = 1,10,1
	do
	    io.stderr:write("foo bar\n")
	end
	EOT
	id=$(flux submit -o userrc=stderr.lua -N1 true) &&
	flux job wait-event -t 30 $id clean &&
	flux job eventlog -f json -p exec $id \
	    | jq -c ". | select(.name==\"log\" and .context.stream==\"stderr\")" \
	      >bgexec-stderr.out &&
	test $(grep -c "foo bar" bgexec-stderr.out) -eq 10
'
test_expect_success 'shell stdout is captured to exec.eventlog' '
	cat >stdout.lua <<-EOT &&
	for i = 1,10,1
	do
	    io.stdout:write("hello out\n")
	end
	EOT
	id=$(flux submit -o userrc=stdout.lua -N1 true) &&
	flux job wait-event -t 30 $id clean &&
	flux job eventlog -f json -p exec $id \
	    | jq -c ". | select(.name==\"log\" and .context.stream==\"stdout\")" \
	      >bgexec-stdout-log.out &&
	test $(grep -c "hello out" bgexec-stdout-log.out) -eq 10
'
test_expect_success 'per-job stats report active shells' '
	id=$(flux submit -N2 -n2 sleep 30) &&
	flux job wait-event -t 30 $id start &&
	flux module stats job-exec \
	    | jq -e ".jobs.\"$id\".active_shells == 2" &&
	flux cancel $id &&
	flux job wait-event -t 30 $id clean
'

# ---------------------------------------------------------------------------
# Test configuration: attributes.system.exec.bgexec object
#
# bgexec reads its test knobs from its own per-implementation namespace
# (attributes.system.exec.bgexec), not the bulk-exec one.
# ---------------------------------------------------------------------------

test_expect_success 'invalid bgexec key in jobspec raises error' '
	flux dmesg -C &&
	test_must_fail flux run --setattr=exec.bgexec.foo=bar true &&
	flux dmesg -H | grep "failed to unpack system.exec.bgexec"
'
test_expect_success 'mock_exception during init terminates job' '
	id=$(flux run --dry-run -N2 -n2 sleep 30 \
	    | jq ".attributes.system.exec.bgexec.mock_exception = \"init\"" \
	    | flux job submit) &&
	flux job wait-event -t 30 $id clean &&
	test_must_fail flux job status $id
'
test_expect_success 'mock_exception while starting terminates job' '
	id=$(flux run --dry-run -N2 -n2 sleep 30 \
	    | jq ".attributes.system.exec.bgexec.mock_exception = \"starting\"" \
	    | flux job submit) &&
	flux job wait-event -t 30 $id clean &&
	test_must_fail flux job status $id
'
test_expect_success 'barrier-timeout drains rank stuck at first barrier' '
	cat >barrier-hang.lua <<-EOT &&
	if shell.info.rank == 1 then
	    os.execute("sleep 30")
	end
	EOT
	id=$(flux run --dry-run -N2 -n2 -o userrc=barrier-hang.lua sleep 60 \
	    | jq ".attributes.system.exec.bgexec.\"barrier-timeout\" = 0.5" \
	    | flux job submit) &&
	flux job wait-event -Ht 60 $id exception &&
	drained=$(flux resource drain -no "{ranks}") &&
	test -n "$drained" &&
	flux resource drain -no "{reason}" | grep "start timeout" &&
	flux resource undrain "$drained" &&
	flux job wait-event -t 30 $id clean
'

# ---------------------------------------------------------------------------
# shell-exit event (shared jobinfo path exercised via bgexec)
# ---------------------------------------------------------------------------

test_expect_success 'single-node job posts shell-exit event' '
	id=$(flux submit -N1 true) &&
	flux job wait-event -p exec -t 30 $id shell-exit &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context.wait_status == 0"
'
test_expect_success 'shell-exit wait_status is nonzero when leader killed' '
	id=$(flux submit -N1 sh -c "kill -9 \$\$") &&
	flux job wait-event -t 30 $id clean &&
	flux job wait-event -p exec --format=json -t 10 $id shell-exit \
	    | jq -e ".context.wait_status != 0"
'

# ---------------------------------------------------------------------------
# cancel and signal delivery
# ---------------------------------------------------------------------------

test_expect_success 'cancel of running job reaches clean' '
	id=$(flux submit -N2 -n2 sleep 60) &&
	flux job wait-event -t 30 $id start &&
	flux cancel $id &&
	flux job wait-event -t 30 $id clean
'

# ---------------------------------------------------------------------------
# reattach is not implemented (matches bulk-exec)
# ---------------------------------------------------------------------------

# TODO: exercise the reattach=ENOSYS path once an instance-restart harness
# for bgexec exists (cf. t3202-instance-restart-testexec.t).

test_expect_success 'reload job-exec to defaults' '
	flux module reload job-exec
'

test_done
