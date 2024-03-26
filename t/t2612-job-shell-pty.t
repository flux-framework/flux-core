#!/bin/sh
#
test_description='Test flux-shell pty support'

. `dirname $0`/sharness.sh

test_under_flux 4 job

# an existing FLUX_TERMINUS_SESSION will interfere with tests below
unset FLUX_TERMINUS_SESSION

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"

INITRC_TESTDIR="${SHARNESS_TEST_SRCDIR}/shell/initrc"
INITRC_PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py -f asciicast"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

shell_leader_rank() {
    flux job wait-event -f json -p exec $1 shell.init | 
        jq '.context["leader-rank"]'
}
shell_service() {
    flux job wait-event -f json -p exec $1 shell.init | \
        jq -r '.context["service"]'
}
terminus_jobid() {
    test $# -lt 2 && return 1
    local jobid=$1
    local cmd=$2
    shift 2
    flux terminus $cmd \
        -r $(shell_leader_rank $jobid) \
        -s $(shell_service $jobid).terminus "$@"
}

test_expect_success 'pty: submit a job with an interactive pty' '
	id=$(flux submit --flags waitable -o pty.interactive tty) &&
	terminus_jobid $id list &&
	$runpty flux job attach ${id} &&
	flux job wait $id
'
test_expect_success NO_CHAIN_LINT 'pty: run job with pty' '
	printf "PS1=XXX:\n" >ps1.rc
	id=$(flux submit -o pty.interactive bash --rcfile ps1.rc | flux job id)
	$runpty -o log.job-pty flux job attach ${id} &
	pid=$! &&
	terminus_jobid ${id} list &&
	$waitfile -t 20 -vp "XXX:" log.job-pty &&
	printf "flux job id \$FLUX_JOB_ID\r" | terminus_jobid ${id} attach -p 0 &&
	$waitfile -t 20 -vp ${id} log.job-pty &&
	printf "exit\r\n" | terminus_jobid ${id} attach -p 0 &&
	$waitfile -t 20 -vp exit log.job-pty &&
	wait $pid
'
# Interactively attach to pty many times to ensure no hangs, etc.
#
test_expect_success NO_CHAIN_LINT 'pty: interactive job with pty' '
	flux submit --cc=1-10 -n1 -o pty.interactive stty size >jobids &&
	for id in $(cat jobids); do $runpty flux job attach $id; done
'

test_expect_success 'pty: client is detached from terminated job' '
	cat <<-EOF >die-test.sh &&
	#!/bin/sh
	# Do not continue until test is ready:
	flux kvs get --waitcreate --count=1 --label test-ready
	# Kill shell so that services immediately go away
	kill -9 \$PPID
	sleep 10
	EOF
	chmod +x die-test.sh &&
	jobid=$(flux submit -n2 -o pty.interactive -o pty.ranks=0 \
	        -o exit-timeout=1 ./die-test.sh) &&
	{ $runpty -w 80x25 -o log.killed \
		flux job attach --show-exec ${jobid} & } &&
	pid=$! &&
	$waitfile -t 20 -vp "shell.start" log.killed &&
	flux kvs put -N job-$(flux job id ${jobid}) test-ready=1 &&
	$waitfile -t 20 -vp "pty disappeared" log.killed &&
	test_must_fail wait $pid
'
test_expect_success 'pty: pty for all tasks, output captured' '
	flux run -n2 --label-io -o pty tty > ptys.out &&
	grep 0: ptys.out &&
	grep 1: ptys.out &&
	test_must_fail grep "not a tty" ptys.out
'
test_expect_success 'pty: ptys only on select ranks' '
	test_expect_code 1 \
		flux run -n2 --label-io -o pty.ranks=1 tty > ptys1.out &&
	grep "0: not a tty" ptys1.out &&
	test_must_fail grep "1: not a tty" ptys1.out
'
test_expect_success 'pty: pty.ranks can take an idset' '
	flux run -n2 --label-io -o pty.ranks=0,1 tty > ptyss.out &&
	grep 0: ptyss.out &&
	grep 1: ptyss.out &&
	test_must_fail grep "not a tty" ptyss.out
'
test_expect_success 'pty: pty.interactive forces a pty on rank 0' '
	id=$(flux submit \
		-o pty.interactive -o pty.ranks=1 \
		-n2 \
		tty) &&
	terminus_jobid $id list &&
	$runpty flux job attach ${id} &&
	flux job eventlog -p output ${id} | grep "adding pty to rank 0"
'
test_expect_success 'pty: -o pty.interactive and -o pty.capture can be used together' '
	for i in $(seq 1 3); do
		id=$(flux submit -o pty.interactive -o pty.capture tty) &&
		$runpty flux job attach $id >ptyim.out 2>&1 &&
		$runpty flux job attach $id &&
		flux job eventlog -f json -p output $id \
			| tail -n1 >last-event.$i
	done &&
	# Check that eof:true is the last event for all runs
	cat last-event.1 | jq -e .context.eof &&
	cat last-event.2 | jq -e .context.eof &&
	cat last-event.3 | jq -e .context.eof
'
test_expect_success 'pty: unsupported -o pty.<opt> generates exception' '
	test_must_fail flux run -o pty.foo hostname
'

test_expect_success 'pty: no hang when invalid command is run under pty' '
	test_expect_code 127 run_timeout 15 \
		flux run -o pty.interactive nosuchcommand
'
# Note: test below uses printf(1) to send [Ctrl-SPACE (NUL), newline, Ctrl-D]
# over the pty connection and expects ^@ (the standard representation of NUL)
# to appear in output.
nul_ctrl_d() {
	python3 -c 'print("\x00\n\x04", end=None)'
}
test_expect_success 'pty: NUL (Ctrl-SPACE) character not dropped' '
	nul_ctrl_d | flux run -vvv -o pty.interactive -o pty.capture cat -v &&
	flux job eventlog -HLp output $(flux job last) &&
	flux job eventlog -HLp output $(flux job last) | grep \\^@
'
test_done
