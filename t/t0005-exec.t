#!/bin/sh
#

test_description='Test broker exec functionality, used by later tests


Test exec functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

rkill="flux sproc kill"
rps="flux sproc ps"
rwait="flux sproc wait"

invalid_rank() {
       echo $((${SIZE} + 1))
}

TMPDIR=$(cd /tmp && $(which pwd))

test_expect_success 'basic exec functionality' '
	flux exec -n true
'

test_expect_success 'exec to specific rank' '
	flux exec -n -r 0 true
'

test_expect_success 'exec to "all" ranks' '
	flux exec -n -r all true
'

test_expect_success 'exec to subset of ranks' '
	flux exec -n -r 2-3 true
'

test_expect_success 'exec to all except a set of ranks' '
	flux exec -n -x 2-3 flux getattr rank | sort -n >ranks_x.out &&
	cat >ranks_x.exp <<-EOT
	0
	1
	EOT
'
test_expect_success 'exec with --ranks and --exclude works' '
	flux exec -r 2-3 -x 3 flux getattr rank &&
	test $(flux exec -r 2-3 -x 3 flux getattr rank) -eq 2
'
test_expect_success 'configure access.allow-guest-user = true' '
	flux config load <<-EOT
	access.allow-guest-user = true
	EOT
'
test_expect_success 'exec to rank 0 from another rank is an error' '
	test_must_fail flux exec -n -r 1 flux exec -n -r 0 true
'
test_expect_success 'configure access.allow-guest-user = false' '
	flux config load <<-EOT
	access.allow-guest-user = false
	EOT
'
test_expect_success 'exec to rank 0 from another rank works' '
	flux exec -n -r 1 flux exec -n -r 0 true
'

test_expect_success 'exec to non-existent rank is an error' '
	test_must_fail flux exec -n -r $(invalid_rank) true
'

test_expect_success 'exec to empty idset is an error' '
	test_must_fail flux exec -n -r 0-1 -x 0-1 true
'

test_expect_success 'test_on_rank works' '
	test_on_rank 1 true
'

test_expect_success 'test_on_rank sends to correct rank' '
	flux getattr rank | grep 0 &&
	test_on_rank 1 sh -c "flux getattr rank | grep -x 1"
'

test_expect_success 'test_on_rank works with test_must_fail' '
	test_must_fail test_on_rank 1 sh -c "flux getattr rank | grep -x 0"
'

test_expect_success 'flux exec passes environment variables' '
	test_must_fail flux exec -n -r 0 sh -c "test \"\$FOOTEST\" = \"t\"" &&
	FOOTEST=t &&
	export FOOTEST &&
	flux exec -n -r 0 sh -c "test \"\$FOOTEST\" = \"t\"" &&
	test_on_rank 0 sh -c "test \"\$FOOTEST\" = \"t\""
'

test_expect_success 'flux exec does not pass FLUX_URI' '
        # Ensure FLUX_URI for rank 1 doesn not equal FLUX_URI for 0
	flux exec -n -r 1 sh -c "test \"\$FLUX_URI\" != \"$FLUX_URI\""
'

test_expect_success 'flux exec passes cwd' '
	(cd ${TMPDIR} &&
	flux exec -n sh -c "test \`pwd\` = \"${TMPDIR}\"")
'

test_expect_success 'flux exec -d option works' '
	flux exec -n -d ${TMPDIR} sh -c "test \`pwd\` = \"${TMPDIR}\""
'

test_expect_success 'flux exec -d none works' '
	(cd /tmp && flux exec -n -d none sh -c "test \$(pwd) != /tmp")
'

# Run a script on ranks 0-3 simultaneously with each rank writing the
#  rank id to a file. After successful completion, the contents of the files
#  are verified to ensure each rank connected to the right broker.
test_expect_success 'test_on_rank works on multiple ranks' '
	output_dir=$(pwd) &&
	rm -f rank_output.* &&
	cat >multiple_rank_test <<EOF &&
rank=\`flux getattr rank\`
echo \$rank > $(pwd)/rank_output.\${rank}
exit 0
EOF
	test_on_rank 0-3 sh $(pwd)/multiple_rank_test &&
	test `cat rank_output.0`  = "0" &&
	test `cat rank_output.1`  = "1" &&
	test `cat rank_output.2`  = "2" &&
	test `cat rank_output.3`  = "3"
'

test_expect_success 'flux exec exits with code 127 for file not found' '
	test_expect_code 127 run_timeout 10 flux exec -n ./nosuchprocess
'

test_expect_success 'flux exec outputs appropriate error message for file not found' '
	test_expect_code 127 flux exec -n ./nosuchprocess 2> exec.stderr &&
	grep "error launching process" exec.stderr &&
	grep "No such file or directory" exec.stderr
'

test_expect_success 'flux exec exits with code 126 for non executable' '
	test_expect_code 126 flux exec -n /dev/null 2> exec.stderr2 &&
	grep "error launching process" exec.stderr2 &&
	grep "Permission denied" exec.stderr2
'

test_expect_success NO_ASAN 'flux exec passes non-zero exit status' '
	test_expect_code 2 flux exec -n sh -c "exit 2" &&
	test_expect_code 3 flux exec -n sh -c "exit 3" &&
	test_expect_code 139 flux exec -n sh -c "kill -11 \$\$"
'

test_expect_success 'flux exec fails with --with-imp if no IMP configured' '
	test_expect_code 1 flux exec --with-imp hostname 2>exec-no-imp.out &&
	test_debug "cat exec-no-imp.out" &&
	grep "exec\.imp path not found in config" exec-no-imp.out
'

test_expect_success NO_ASAN 'flux exec outputs tasks with errors' '
	! flux exec -n sh -c "exit 2" > 2.out 2>&1 &&
        grep "\[0-3\]: Exit 2" 2.out &&
	! flux exec -n sh -c "exit 3" > 3.out 2>&1 &&
        grep "\[0-3\]: Exit 3" 3.out &&
	! flux exec -n sh -c "kill -11 \$\$" > 139.out 2>&1 &&
        grep "\[0-3\]: Segmentation fault" 139.out
'

test_expect_success 'basic IO testing' '
	flux exec -n -r0 echo Hello | grep ^Hello\$  &&
	flux exec -n -r0 sh -c "echo Hello >&2" 2>stderr &&
	cat stderr | grep ^Hello\$
'

test_expect_success 'per rank output works' '
	flux exec -n -r 1 sh -c "flux getattr rank" | grep -x 1 &&
	flux exec -n -lr 2 sh -c "flux getattr rank" | grep -x "2: 2" &&
	cat >expected <<EOF &&
0: 0
1: 1
2: 2
3: 3
EOF
	flux exec -n -lr 0-3 sh -c "flux getattr rank" | sort -n >output &&
	test_cmp output expected
'

test_expect_success 'I/O, multiple lines, no newline on last line' '
	/bin/echo -en "1: one\n1: two" >expected &&
	flux exec -n -lr 1 /bin/echo -en "one\ntwo" >output &&
	test_cmp output expected &&
	/bin/echo -en "1: one" >expected &&
	flux exec -n -lr 1 /bin/echo -en "one" >output &&
	test_cmp output expected
'

test_expect_success 'I/O -- long lines' '
	dd if=/dev/urandom bs=4096 count=1 | base64 --wrap=0 >expected &&
	flux exec -n -r1 cat expected > output &&
	test_cmp output expected
'

waitfile=$SHARNESS_TEST_SRCDIR/scripts/waitfile.lua
test_expect_success 'signal forwarding works' '
	cat >test_signal.sh <<-EOF &&
	#!/bin/bash
	sig=\${1-INT}
	rm -f sleepready.out
	mkfifo input.fifo
	stdbuf --output=L \
	    flux exec -v -n awk "BEGIN {print \"hi\"} {print}" input.fifo \
	        >sleepready.out &
	$waitfile -vt 20 -p ^hi -c ${SIZE} sleepready.out &&
	kill -\$sig %1 &&
	wait %1
	exit \$?
	EOF
	chmod +x test_signal.sh &&
	test_expect_code 130 run_timeout 20 ./test_signal.sh INT &&
	test_expect_code 143 run_timeout 20 ./test_signal.sh TERM
'

test_expect_success 'flux-exec: stdin bcast to all ranks (default)' '
	count=$(echo Hello | flux exec cat | grep -c Hello) &&
	test "$count" = "4"
'

test_expect_success 'flux-exec: stdin bcast to all ranks (all)' '
	count=$(echo Hello | flux exec -rall cat | grep -c Hello) &&
	test "$count" = "4"
'

test_expect_success 'flux-exec: stdin bcast to all ranks (0-3)' '
	count=$(echo Hello | flux exec -r0-3 cat | grep -c Hello) &&
	test "$count" = "4"
'

test_expect_success 'flux-exec: stdin bcast to 1 rank' '
	count=$(echo Hello | flux exec -r1 cat | grep -c Hello) &&
	test "$count" = "1"
'

test_expect_success 'flux-exec: stdin bcast to not all ranks' '
	count=$(echo Hello | flux exec -r0-2 cat | grep -c Hello) &&
	test "$count" = "3"
'

test_expect_success 'stdin redirect from /dev/null works without -n' '
	test_expect_code 0 run_timeout 10 flux exec -r0-3 cat
'

test_expect_success 'stdin redirect from /dev/null works with -n' '
	test_expect_code 0 run_timeout 10 flux exec -n -r0-3 cat
'

test_expect_success 'create large file for tests' '
	dd if=/dev/urandom of=5Mfile bs=5M count=1
'

test_expect_success 'create test script to redirect stdin to a file' '
	cat <<-EOT >stdin2file &&
	#!/bin/bash
	rank=\$(flux getattr rank)
	dd of=cpy.\$rank
	EOT
	chmod +x stdin2file
'

# piping a 5M file using a 4K buffer should overflow if flow control
# is not functioning correctly
test_expect_success 'stdin flow control works (1 rank)' '
	cat 5Mfile | flux exec -r 0 --setopt=stdin_BUFSIZE=4096 ./stdin2file &&
	cmp 5Mfile cpy.0 &&
	rm cpy.0
'

test_expect_success 'stdin flow control works (all ranks)' '
	cat 5Mfile | flux exec -r 0-3 --setopt=stdin_BUFSIZE=4096 ./stdin2file &&
	cmp 5Mfile cpy.0 &&
	cmp 5Mfile cpy.1 &&
	cmp 5Mfile cpy.2 &&
	cmp 5Mfile cpy.3 &&
	rm cpy.*
'

test_expect_success 'create test script to redirect stdin to a file, one rank exits early' '
	cat <<-EOT >stdin2file &&
	#!/bin/bash
	rank=\$(flux getattr rank)
	if test \$rank -ne 0; then
		dd of=cpy.\$rank
	fi
	EOT
	chmod +x stdin2file
'

test_expect_success 'stdin flow control works (all ranks, one rank will exit early)' '
	cat 5Mfile | flux exec -r 0-3 --setopt=stdin_BUFSIZE=4096 ./stdin2file &&
	test_must_fail ls cpy.0 &&
	cmp 5Mfile cpy.1 &&
	cmp 5Mfile cpy.2 &&
	cmp 5Mfile cpy.3 &&
	rm cpy.*
'

test_expect_success 'stdin broadcast -- multiple lines' '
	dd if=/dev/urandom bs=1024 count=4 | base64 >expected &&
	cat expected | run_timeout 10 flux exec -l -r0-3 cat >output &&
	for i in $(seq 0 3); do
		sed -n "s/^$i: //p" output > output.$i &&
		test_cmp expected output.$i
	done
'

test_expect_success 'stdin broadcast -- long lines' '
	dd if=/dev/urandom bs=1024 count=4 | base64 --wrap=0 >expected &&
        echo >>expected &&
	cat expected | run_timeout 10 flux exec -l -r0-3 cat >output &&
	for i in $(seq 0 3); do
		sed -n "s/^$i: //p" output > output.$i
		test_cmp expected output.$i
	done
'

test_expect_success 'dbus environment variable is set' '
	DBUS_SESSION_BUS_ADDRESS= \
	    flux exec -r 0 printenv DBUS_SESSION_BUS_ADDRESS
'
test_expect_success 'dbus environment variable is not overwritten if set' '
	DBUS_SESSION_BUS_ADDRESS=xyz \
	    flux exec -r 0 printenv DBUS_SESSION_BUS_ADDRESS >dbus.out &&
	cat >dbus.exp <<-EOT &&
	xyz
	EOT
	test_cmp dbus.exp dbus.out
'
test_expect_success 'create check-pid.py python script' '
	cat <<-EOF >check-pid.py
	import sys
	import flux

	rank = int(sys.argv[1])
	pid = int(sys.argv[2])

	resp = flux.Flux().rpc("rexec.list", nodeid=rank).get()
	for proc in resp["procs"]:
	    remote_pid = proc["pid"]
	    if remote_pid == pid:
	        print(f"found pid={pid}=={remote_pid}", file=sys.stderr)
	        sys.exit(0)
	print(f"pid {pid} not found on rank {rank}", file=sys.stderr)
	sys.exit(1)
	EOF
'
test_expect_success 'rexec: --bg option works' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r0 --bg sleep 10)
	EOF
	test_debug "echo started pid $pid on rank $rank" &&
	flux python check-pid.py $rank $pid
'
dmesg_grep=${SHARNESS_TEST_SRCDIR}/scripts/dmesg-grep.py
test_expect_success 'rexec: terminate bg pid and exit status is logged' '
	$rkill -r $rank 9 $pid &&
	$dmesg_grep -t10 "sleep\\[$pid\\]: Killed by signal 9" &&
	test_must_fail flux python check-pid.py $rank $pid
'
test_expect_success 'rexec: --bg logs output, exit code to broker logs' '
	flux exec -r 0 --bg \
	    sh -c "echo some stdout; echo some stderr >&2; exit 1" &&
	$dmesg_grep -t10 "rexec.*sh.*: some stdout" &&
	$dmesg_grep -t10 "rexec.*sh.*: some stderr" &&
	$dmesg_grep -t10 "rexec.*sh.*: Exit 1"
'
test_expect_success 'rexec: --bg with non-existent command fails' '
	test_must_fail flux exec -r 0 --bg nosuchcommand &&
	test_must_fail flux exec --bg nosuchcommand
'
test_expect_success 'rexec: zero-length --label fails' '
	test_must_fail flux exec --label="" true
'
test_expect_success 'rexec: --label works' '
	flux exec -r 0 --bg --label=foo sleep 300 &&
	$rps | grep foo
'
test_expect_success 'rexec: --label fails with pre-existing label' '
	test_must_fail flux exec -r 0 --label=foo true
'
test_expect_success 'rexec: can kill process by label' '
	$rkill -r 0 15 foo  &&
	$dmesg_grep -t10 "foo: sleep\\[.*\\]: Killed by signal 15"
'
test_expect_success 'rexec: kill of unknown label fails' '
	test_must_fail $rkill -r 0 15 badlabel
'
test_expect_success 'rexec: wait RPC fails for nonexistent pid' '
	test_must_fail $rwait -r 0 1234
'
test_expect_success 'rexec: wait RPC fails for nonexistent label' '
	test_must_fail $rwait -r 0 badlabel
'
test_expect_success 'rexec: --waitable requires --bg' '
	test_must_fail flux exec -r 0 --waitable sleep 0
'
test_expect_success 'rexec: waitable bg failed proc does not become zombie' '
	test_must_fail flux exec -r 0 --bg --waitable nosuchcommand &&
	$rps > waitable-fail-ps.out &&
	test_debug "cat waitable-fail-ps.out" &&
	test_must_fail grep nosuchcommand waitable-fail-ps.out
'
test_expect_success 'rexec: subprocesses can be waitable' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r0 --bg --waitable sleep 0)
	EOF
	test_debug "echo started waitable process $pid on rank $rank" &&
	$rps &&
	$rps | grep $pid &&
	$rwait $pid
'
test_expect_success 'rexec: waitable flag rejected for streaming rpc' '
	cat <<-EOF >waitable-streaming.py &&
	import os
	import sys
	import flux
	from flux.constants import FLUX_RPC_STREAMING

	FLUX_SUBPROCESS_FLAGS_WAITABLE = 16

	cmd = dict(
	    cmdline=["true"],
            cwd=os.getcwd(),
	    channels=[],
	    opts={},
            env=dict(os.environ)
	)
	flux.Flux().rpc(
	    "rexec.exec",
	    payload={"cmd": cmd, "flags": FLUX_SUBPROCESS_FLAGS_WAITABLE},
	    flags=FLUX_RPC_STREAMING,
	).get()
	EOF
	test_must_fail flux python waitable-streaming.py 2>wait-stream.err &&
	test_debug "cat wait-stream.err" &&
	grep "only supported in background mode" wait-stream.err
'
test_expect_success 'rexec: waitable subprocess returns nonzero status' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r0 --bg --waitable false)
	EOF
	test_debug "echo started waitable process $pid on rank $rank" &&
	$rps &&
	$rps | grep $pid &&
	test_expect_code 1 $rwait $pid
'
test_expect_success 'rexec: wait for active process works' '
	flux exec -r 0 --label=test --bg --waitable sleep inf &&
	$rps &&
	$rps | grep test &&
	test_expect_code 143 $rkill --wait -r 0 15 test
'
test_expect_success 'rexec: create wait+disconnect test utility' '
	cat <<-EOF >wait-disconnect.py
	import sys
	import flux
	f = flux.Flux().rpc("rexec.wait", {"pid":-1, "label":sys.argv[1]})
	EOF
'
test_expect_success 'rexec: wait disconnect cancels waiter' '
	flux exec -r 0 --label=test --bg --waitable sleep inf &&
	$rps | grep test &&
	flux python wait-disconnect.py test &&
	$rps | grep test &&
	test_expect_code 143 $rkill --wait -r 0 15 test
'
test_expect_success 'rexec: multiple waiters on same process fails' '
	cat <<-EOF >double-wait.py &&
	import sys
	import flux
	h = flux.Flux()
	# Start first wait
	f1 = h.rpc("rexec.wait", {"pid":-1, "label":"double-wait"})
	# Try to start second wait - should fail
	try:
	    f2 = h.rpc("rexec.wait", {"pid":-1, "label":"double-wait"})
	    f2.get()
	    sys.exit(1)  # Should not reach here
	except OSError as e:
	    if e.errno != 22:  # EINVAL
	        raise
	    print(f"Got expected error: {e}")
	EOF
	flux exec -r 0 --label=double-wait --bg --waitable sleep inf &&
	flux python double-wait.py &&
	$rkill -r 0 15 double-wait
'
test_expect_success 'rexec: wait on already-reaped process fails' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r0 --bg --waitable sleep 0)
	EOF
	$rwait $pid &&
	test_must_fail $rwait $pid
'
test_expect_success 'rexec: wait on non-waitable process fails' '
	flux exec -r0 --bg --label=non-waitable sleep inf  &&
	test_must_fail $rwait non-waitable &&
	$rkill -r 0 15 non-waitable
'
test_expect_success 'rexec: wait fails for non-background process' '
	flux exec -r 0 --label=stream-wait sleep 0 &&
	$rps > stream-wait-ps.out &&
	test_debug "cat stream-wait-ps.out" &&
	test_must_fail grep stream-wait stream-wait-ps.out
'
test_expect_success 'rexec: wait by label works' '
	flux exec -r 0 --label=wait-by-label --bg --waitable false &&
	$rps | grep wait-by-label &&
	test_expect_code 1 $rwait wait-by-label
'
test_expect_success 'rexec: zombie processes shown with Z state' '
	IFS=": " read -r rank pid <<-EOF &&
	$(flux exec -r0 --bg --waitable sleep 0)
	EOF
	$rps > zombie-state.out &&
	test_debug "cat zombie-state.out" &&
	grep "$pid.*Z" zombie-state.out &&
	$rwait $pid
'
test_expect_success 'rexec: running processes shown with R state' '
	flux exec -r 0 --label=running --bg --waitable sleep inf &&
	$rps > running-state.out &&
	test_debug "cat running-state.out" &&
	grep "R.*running" running-state.out &&
	$rkill -r 0 15 running
'
test_expect_success 'rexec: wait returns correct exit status for killed process' '
	flux exec -r 0 --label=sigterm-test --bg --waitable sleep inf &&
	test_expect_code 143 $rkill --wait -r 0 15 sigterm-test
'
test_expect_success 'rexec: wait returns correct exit status for signal 9' '
	flux exec -r 0 --label=sigkill-test --bg --waitable sleep inf &&
	test_expect_code 137 $rkill --wait -r 0 9 sigkill-test
'
test_expect_success 'rexec: server shutdown works with zombie processes' '
	cat <<-EOF >shutdown-zombies.sh &&
	#!/bin/bash
	# Start instance with subprocess server
	cat <<-EOF2 >shutdown-test.sh &&
		# Create zombie processes
		flux exec -r 0 --bg --waitable --label=z1 sleep 0
		flux exec -r 0 --bg --waitable --label=z2 sleep 0
		flux exec -r 0 --bg --waitable --label=z3 sleep 0
		# Verify zombies exist
		flux sproc ps | grep "Z"
		# Exit - should cleanup zombies during shutdown
	EOF2
	flux start -s 1 bash shutdown-test.sh
	# If we get here, shutdown succeeded
	EOF
	chmod +x shutdown-zombies.sh &&
	./shutdown-zombies.sh
'
test_expect_success 'rexec: failed processes are not kept as zombies' '
	test_must_fail flux exec -r 0 --bg --waitable /nonexistent/command &&
	$rps > failed-zombie.out &&
	test_debug "cat failed-zombie.out" &&
	test_must_fail grep nonexistent failed-zombie.out
'
test_done
