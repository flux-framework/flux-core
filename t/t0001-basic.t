#!/bin/sh
#

test_description='Test the very basics

Ensure the very basics of flux commands work.
This suite verifies functionality that may be assumed working by
other tests.
'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'TEST_NAME is set' '
	test -n "$TEST_NAME"
'
test_expect_success 'run_timeout works' '
	test_expect_code 142 run_timeout 0.001 sleep 2
'
test_expect_success 'test run_timeout with success' '
	run_timeout 0.01 /bin/true
'
test_expect_success 'run_timeout fails if exec fails' '
	test_must_fail run_timeout 0.01 /nonexistent/executable
'
test_expect_success 'we can find a flux binary' '
	flux --help >/dev/null
'
test_expect_success 'flux-keygen works' '
	umask 077 && tmpkeydir=`mktemp -d` &&
	flux keygen --secdir $tmpkeydir --force &&
	rm -rf $tmpkeydir
'

test_expect_success 'flux-python command runs a python that finds flux' '
	flux python -c "import flux"
'

# None of the individual tests should run over 10s
ARGS="-o -Sinit.rc2_timeout=10"

#
# N.B. since we have many back to back flux-starts within this test,
# and each instance runs out the shutdown grace period per issue #1006,
# the net effect is a really slow test.
#
# Until #1006 is fixed, work around this by forcing an unnaturally
# short shutdown grace period.  It means we're interrupting the tear
# down process, but perhaps that's not so much relevant to verifying
# basic functionality...
#
BUG1006="-o,--shutdown-grace=0.1"
ARGS="$ARGS $BUG1006"

# Minimal is sufficient for these tests, but test_under_flux unavailable
# clear the RC paths
export FLUX_RC1_PATH=""
export FLUX_RC3_PATH=""

test_expect_success 'broker --shutdown-grace option works' '
	flux start -o,--shutdown-grace=0.1 -o -Sbroker.rc1_path=/bin/true /bin/true
'
test_expect_success 'flux-start in exec mode works' "
	flux start ${ARGS} 'flux comms info' | grep 'size=1'
"
test_expect_success 'flux-start in subprocess/pmi mode works (size 1)' "
	flux start ${ARGS} --size=1 'flux comms info' | grep 'size=1'
"
test_expect_success 'flux-start in subprocess/pmi mode works (size 2)' "
	flux start ${ARGS} --size=2 'flux comms info' | grep 'size=2'
"
test_expect_success 'flux-start with size 1 has no peers' "
	flux start ${ARGS} --size=1 'flux comms idle' > idle.out &&
        ! grep 'idle' idle.out
"
test_expect_success 'flux-start with size 2 has a peer' "
	flux start ${ARGS} --size=2 'flux comms idle' > idle.out &&
        grep 'idle' idle.out
"
test_expect_success 'flux-start --size=1 --bootstrap=selfpmi works' "
	flux start ${ARGS} --size=1 --bootstrap=selfpmi /bin/true
"
test_expect_success 'flux-start --bootstrap=selfpmi fails (no size specified)' "
	test_must_fail flux start ${ARGS} --bootstrap=selfpmi /bin/true
"
test_expect_success 'flux-start --size=1 --boostrap=pmi fails' "
	test_must_fail flux start ${ARGS} --size=1 --bootstrap=pmi /bin/true
"
test_expect_success 'flux-start in exec mode passes through errors from command' "
	test_must_fail flux start ${ARGS} /bin/false
"
test_expect_success 'flux-start in subprocess/pmi mode passes through errors from command' "
	test_must_fail flux start ${ARGS} --size=1 /bin/false
"
test_expect_success 'flux-start in exec mode passes exit code due to signal' "
	test_expect_code 130 flux start ${ARGS} 'kill -INT \$\$'
"
test_expect_success 'flux-start in subprocess/pmi mode passes exit code due to signal' "
	test_expect_code 130 flux start ${ARGS} --size=1 'kill -INT \$\$'
"
test_expect_success 'flux-start in exec mode works as initial program' "
	flux start --size=2 flux start ${ARGS} flux comms info | grep size=1
"
test_expect_success 'flux-start in subprocess/pmi mode works as initial program' "
	flux start --size=2 flux start ${ARGS} --size=1 flux comms info | grep size=1
"

test_expect_success 'flux-start init.rc2_timeout attribute works' "
	test_expect_code 143 flux start ${ARGS} -o,-Sinit.rc2_timeout=0.1 sleep 5
"
test_expect_success 'flux-start --wrap option works' '
	broker_path=$(flux start -vX 2>&1 | sed "s/^flux-start: *//g") &&
	echo broker_path=${broker_path} &&
	test -n "${broker_path}" &&
	flux start --wrap=/bin/echo,start: arg0 arg1 arg2 > wrap.output &&
	test_debug "cat wrap.output" &&
	cat >wrap.expected <<-EOF &&
	start: ${broker_path} arg0 arg1 arg2
	EOF
	test_cmp wrap.expected wrap.output
'
test_expect_success 'flux-start --wrap option works with --size' '
	flux start --size=2 -vX --wrap=test-wrap > wrap2.output 2>&1 &&
	test_debug "cat wrap2.output" &&
	test "$(grep -c test-wrap wrap2.output)" = "2"
'

test_expect_success 'test_under_flux works' '
	echo >&2 "$(pwd)" &&
        mkdir -p test-under-flux && (
        cd test-under-flux &&
	SHARNESS_TEST_DIRECTORY=`pwd` &&
	export SHARNESS_TEST_SRCDIR SHARNESS_TEST_DIRECTORY FLUX_BUILD_DIR debug &&
	run_timeout 10 "$SHARNESS_TEST_SRCDIR"/test-under-flux/test.t --verbose --debug >out 2>err
	) &&
	grep "size=2" test-under-flux/out
'

test_expect_success 'test_under_flux fails if loaded modules are not unloaded' '
        mkdir -p test-under-flux && (
        cd test-under-flux &&
	SHARNESS_TEST_DIRECTORY=`pwd` &&
	export SHARNESS_TEST_SRCDIR SHARNESS_TEST_DIRECTORY FLUX_BUILD_DIR debug &&
	test_expect_code 1 "$SHARNESS_TEST_SRCDIR"/test-under-flux/t_modcheck.t 2>err.modcheck \
			| grep -v sharness: >out.modcheck
	) &&
	test_cmp "$SHARNESS_TEST_SRCDIR"/test-under-flux/expected.modcheck test-under-flux/out.modcheck
'

test_expect_success 'flux-start -o,--setattr ATTR=VAL can set broker attributes' '
	ATTR_VAL=`flux start ${ARGS} -o,--setattr=foo-test=42 flux getattr foo-test` &&
	test $ATTR_VAL -eq 42
'
test_expect_success 'tbon.endpoint can be read' '
	ATTR_VAL=`flux start flux getattr tbon.endpoint` &&
        echo $ATTR_VAL | grep "^tcp"
'
test_expect_success 'tbon.endpoint can be set and %h works' '
	ATTR_VAL=`flux start -o,--setattr=tbon.endpoint='tcp://%h:*' flux getattr tbon.endpoint` &&
        echo $ATTR_VAL | grep "^tcp" &&
        ! echo $ATTR_VAL | grep "%h"
'
test_expect_success 'tbon.endpoint with %B works' '
	ATTR_VAL=`flux start -o,--setattr=tbon.endpoint='ipc://%B/req' flux getattr tbon.endpoint` &&
        echo $ATTR_VAL | grep "^ipc" &&
        ! echo $ATTR_VAL | grep "%B"
'
test_expect_success 'tbon.endpoint fails on bad endpoint' '
	! flux start -o,--setattr=tbon.endpoint='foo://bar' flux getattr tbon.endpoint
'
test_expect_success 'tbon.parent-endpoint cannot be read on rank 0' '
       ! flux start flux getattr tbon.parent-endpoint
'
test_expect_success 'tbon.parent-endpoint can be read on not rank 0' '
       NUM=`flux start --size 4 flux exec -n flux getattr tbon.parent-endpoint | grep ipc | wc -l` &&
       test $NUM -eq 3
'
test_expect_success 'flux start --bootstrap=pmi (singlton) cleans up broker.rundir' '
	flux start ${ARGS} --bootstrap=pmi \
		flux getattr broker.rundir >rundir_pmi.out &&
	RUNDIR=$(cat rundir_pmi.out) &&
	test_must_fail test -d $RUNDIR
'
test_expect_success 'flux start --bootstrap=selfpmi --size=1 cleans up broker.rundirs' '
	flux start ${ARGS} --bootstrap=selfpmi --size=1 \
		flux getattr broker.rundir >rundir_selfpmi1.out &&
	RUNDIR=$(cat rundir_selfpmi1.out) &&
	test -n "$RUNDIR" &&
	test_must_fail test -d $(dirname $RUNDIR)
'
test_expect_success 'flux start --bootstrap=selfpmi --size=2 cleans up broker.rundirs' '
	flux start ${ARGS} --bootstrap=selfpmi --size=2 \
		flux getattr broker.rundir >rundir_selfpmi2.out &&
	RUNDIR=$(cat rundir_selfpmi2.out) &&
	test -n "$RUNDIR" &&
	test_must_fail test -d $(dirname $RUNDIR)
'
test_expect_success 'broker.rundir override works' '
	RUNDIR=`mktemp -d` &&
	DIR=`flux start ${ARGS} -o,--setattr=broker.rundir=$RUNDIR flux getattr broker.rundir` &&
	test "$DIR" = "$RUNDIR" &&
	test -d $RUNDIR &&
	rmdir $RUNDIR
'
test_expect_success 'broker.rundir override creates nonexistent dirs' '
	RUNDIR="$(pwd)/rundir" &&
	flux start ${ARGS} -o,--setattr=broker.rundir=$RUNDIR sh -c "test -d $RUNDIR" &&
	test_expect_code 1 test -d $RUNDIR
'
test_expect_success 'broker persist-directory works' '
	PERSISTDIR=`mktemp -d` &&
	flux start ${ARGS} -o,--setattr=persist-directory=$PERSISTDIR /bin/true &&
	test -d $PERSISTDIR &&
	test `ls -1 $PERSISTDIR|wc -l` -gt 0 &&
	rm -rf $PERSISTDIR
'
test_expect_success 'broker persist-filesystem works' '
	PERSISTFS=`mktemp -d` &&
	PERSISTDIR=`flux start ${ARGS} -o,--setattr=persist-filesystem=$PERSISTFS flux getattr persist-directory` &&
	test -d $PERSISTDIR &&
	test `ls -1 $PERSISTDIR|wc -l` -gt 0 &&
	rm -rf $PERSISTDIR &&
	test -d $PERSISTFS &&
	rmdir $PERSISTFS
'
test_expect_success 'broker persist-filesystem is ignored if persist-directory set' '
	PERSISTFS=`mktemp -d` &&
	PERSISTDIR=`mktemp -d` &&
	DIR=`flux start ${ARGS} -o,--setattr=persist-filesystem=$PERSISTFS,--setattr=persist-directory=$PERSISTDIR \
		flux getattr persist-directory` &&
	test "$DIR" = "$PERSISTDIR" &&
	test `ls -1 $PERSISTDIR|wc -l` -gt 0 &&
	rmdir $PERSISTFS &&
	rm -rf $PERSISTDIR
'
# Use -eq hack to test that BROKERPID is a number
test_expect_success 'broker broker.pid attribute is readable' '
	BROKERPID=`flux start ${ARGS} flux getattr broker.pid` &&
	test -n "$BROKERPID" &&
	test "$BROKERPID" -eq "$BROKERPID"
'
test_expect_success 'broker broker.pid attribute is immutable' '
	test_must_fail flux start ${ARGS} -o,--setattr=broker.pid=1234 flux getattr broker.pid
'
test_expect_success 'broker --verbose option works' '
	flux start ${ARGS} -o,-v /bin/true
'
test_expect_success 'broker --heartrate option works' '
	flux start ${ARGS} -o,--heartrate=0.1 /bin/true
'
test_expect_success 'broker --k-ary option works' '
	flux start ${ARGS} -s4 -o,--k-ary=1 /bin/true &&
	flux start ${ARGS} -s4 -o,--k-ary=2 /bin/true &&
	flux start ${ARGS} -s4 -o,--k-ary=3 /bin/true &&
	flux start ${ARGS} -s4 -o,--k-ary=4 /bin/true
'

test_expect_success 'flux-help command list can be extended' '
	mkdir help.d &&
	cat <<-EOF  > help.d/test.json &&
	[{ "category": "test", "command": "test", "description": "a test" }]
	EOF
	flux help 2>&1 | sed "0,/^$/d" >help.expected &&
	cat <<-EOF  >>help.expected &&
	Common commands from flux-test:
	   test               a test
	EOF
	FLUX_CMDHELP_PATTERN="help.d/*" flux help 2>&1 | sed "0,/^$/d" > help.out &&
	test_cmp help.expected help.out &&
	cat <<-EOF  > help.d/test2.json &&
	[{ "category": "test2", "command": "test2", "description": "a test two" }]
	EOF
	cat <<-EOF  >>help.expected &&

	Common commands from flux-test2:
	   test2              a test two
	EOF
	FLUX_CMDHELP_PATTERN="help.d/*" flux help 2>&1 | sed "0,/^$/d" > help.out &&
	test_cmp help.expected help.out
'
test_expect_success 'flux-help command can display manpages for subcommands' '
	PWD=$(pwd) &&
	mkdir -p man/man1 &&
	cat <<-EOF > man/man1/flux-foo.1 &&
	.TH FOO "1" "January 1962" "Foo utils" "User Commands"
	.SH NAME
	foo \- foo bar baz
	EOF
	MANPATH=${PWD}/man FLUX_IGNORE_NO_DOCS=y flux help foo | grep "^FOO(1)"
'
test_expect_success 'flux-help command can display manpages for api calls' '
	PWD=$(pwd) &&
	mkdir -p man/man3 &&
	cat <<-EOF > man/man3/flux_foo.3 &&
	.TH FOO "3" "January 1962" "Foo api call" "Flux Programming Interface"
	.SH NAME
	flux_foo \- Call the flux_foo interface
	EOF
	MANPATH=${PWD}/man FLUX_IGNORE_NO_DOCS=y flux help flux_foo | grep "^FOO(3)"
'
missing_man_code()
{
        man notacommand >/dev/null 2>&1
        echo $?
}
test_expect_success 'flux-help returns nonzero exit code from man(1)' '
        test_expect_code $(missing_man_code) \
                         eval FLUX_IGNORE_NO_DOCS=y flux help notacommand
'
test_expect_success 'flux appends colon to missing or unset MANPATH' '
      (unset MANPATH && flux /usr/bin/printenv | grep "MANPATH=.*:$") &&
      MANPATH= flux /usr/bin/printenv | grep "MANPATH=.*:$"
'
test_expect_success 'builtin test_size_large () works' '
    size=$(test_size_large)  &&
    test -n "$size" &&
    size=$(FLUX_TEST_SIZE_MAX=2 test_size_large) &&
    test "$size" = "2" &&
    size=$(FLUX_TEST_SIZE_MIN=12345 FLUX_TEST_SIZE_MAX=23456 test_size_large) &&
    test "$size" = "12345"
'

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua
test_expect_success 'scripts/waitfile works' '
	flux start $waitfile -v -t 5 -p "hello" waitfile.test.1 &
	p=$! &&
	echo "hello" > waitfile.test.1 &&
	wait $p
'

test_expect_success 'scripts/waitfile works after <1s' '
	flux start $waitfile -v -t 2 -p "hello" -P- waitfile.test.2 <<-EOF &
	-- open file at 250ms, write pattern at 500ms
	f:timer{ timeout = 250,
	         handler = function () tf = io.open ("waitfile.test.2", "w") end
	}
	f:timer{ timeout = 500,
	         handler = function () tf:write ("hello\n"); tf:flush() end
	}
	EOF
	p=$! &&
	wait $p
'

test_expect_success 'scripts/waitfile works after 1s' '
	flux start $waitfile -v -t 5 -p "hello" -P- waitfile.test.3 <<-EOF &
	-- Wait 250ms and create file, at .5s write a line, at 1.1s write pattern:
	f:timer{ timeout = 250,
	         handler = function () tf = io.open ("waitfile.test.3", "w") end
               }
	f:timer{ timeout = 500,
	         handler = function () tf:write ("line one"); tf:flush()  end
	       }
	f:timer{ timeout = 1100,
	         handler = function () tf:write ("hello\n"); tf:flush() end
	       }
	EOF
	p=$! &&
	wait $p
'
# test for issue #1025
test_expect_success 'instance can stop cleanly with subscribers (#1025)' '
	flux start ${ARGS} -s2 --bootstrap=selfpmi bash -c "nohup flux event sub hb &"
'

# test for issue #1191
test_expect_success 'passing NULL to flux_log functions logs to stderr (#1191)' '
        ${FLUX_BUILD_DIR}/t/loop/logstderr > std.out 2> std.err &&
        grep "warning: hello" std.err &&
        grep "err: world: No such file or directory" std.err
'

reactorcat=${SHARNESS_TEST_DIRECTORY}/reactor/reactorcat
test_expect_success 'reactor: reactorcat example program works' '
	dd if=/dev/urandom bs=1024 count=4 >reactorcat.in &&
	$reactorcat <reactorcat.in >reactorcat.out &&
	test_cmp reactorcat.in reactorcat.out &&
	$reactorcat </dev/null >reactorcat.devnull.out &&
	test -f reactorcat.devnull.out &&
	test_must_fail test -s reactorcat.devnull.out
'

test_expect_success 'flux-start: panic rank 1 of a size=2 instance' '
	! flux start --killer-timeout=0.2 --bootstrap=selfpmi --size=2 \
		bash -c "flux getattr broker.rundir; flux comms -r 1 panic fubar; sleep 5" >panic.out 2>panic.err
'
test_expect_success 'flux-start: panic message reached stderr' '
	grep -q fubar panic.err
'
# flux-start: 1 (pid 10023) exited with rc=1
test_expect_success 'flux-start: rank 1 exited with rc=1' '
	egrep "flux-start: 1 .* exited with rc=1" panic.err
'
# flux-start: 0 (pid 21474) Killed
test_expect_success 'flux-start: rank 0 Killed' '
	egrep "flux-start: 0 .* Killed" panic.err
'

# Usage: cleanup_tmpdir tmpdir size
# Remove tmpdir and expected contents.
# Could be replaced with rm -rf $tmpdir
cleanup_tmpdir () {
	local topdir=$1
	local size=$2
	local rank

	for rank in $(seq 0 $(($size-1))); do
		rm -f $topdir/${rank}/local
		rm -f $topdir/${rank}/req
		rmdir $topdir/${rank} || :
	done
	rmdir $topdir || :
}

test_expect_success 'flux-start: cleanup testdir after panic _exit()' '
	tmpdir=$(dirname $(cat panic.out)) &&
	test -n "$tmpdir" &&
	cleanup_tmpdir ${tmpdir} 2
'

test_done
