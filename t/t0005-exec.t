#!/bin/sh
#

test_description='Test broker exec functionality, used by later tests


Test exec functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

invalid_rank() {
       echo $((${SIZE} + 1))
}

TMPDIR=$(cd /tmp && $(which pwd))

test_expect_success 'basic exec functionality' '
	flux exec -n /bin/true
'

test_expect_success 'exec to specific rank' '
	flux exec -n -r 0 /bin/true
'

test_expect_success 'exec to "all" ranks' '
	flux exec -n -r all /bin/true
'

test_expect_success 'exec to subset of ranks' '
	flux exec -n -r 2-3 /bin/true
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
	test_must_fail flux exec -n -r 1 flux exec -n -r 0 /bin/true
'
test_expect_success 'configure access.allow-guest-user = false' '
	flux config load <<-EOT
	access.allow-guest-user = false
	EOT
'
test_expect_success 'exec to rank 0 from another rank works' '
	flux exec -n -r 1 flux exec -n -r 0 /bin/true
'

test_expect_success 'exec to non-existent rank is an error' '
	test_must_fail flux exec -n -r $(invalid_rank) /bin/true
'

test_expect_success 'exec to empty idset is an error' '
	test_must_fail flux exec -n -r 0-1 -x 0-1 /bin/true
'

test_expect_success 'test_on_rank works' '
	test_on_rank 1 /bin/true
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

test_done
