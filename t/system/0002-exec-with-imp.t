#
#  flux-exec(1) --with-imp option works
#
IMP=$(flux config get exec.imp)
test -n "$IMP" && test_set_prereq HAVE_IMP

SIZE=$(flux getattr size)

test_expect_success HAVE_IMP 'configure a flux-imp run command' '
	confdir="/etc/flux/imp/conf.d" &&
	cat <<-EOF >imp-run-test.toml &&
	[run.test]
	allowed-users = ["flux"]
	allowed-environment = ["FLUX_*", "TEST_ARG"]
	path = "$confdir/run-test.sh"
	EOF
	sudo cp imp-run-test.toml $confdir &&
	cleanup "sudo rm -f $confdir/imp-run-test.toml" &&
	sudo chmod 644 $confdir/imp-run-test.toml &&
	cat <<-EOF >run-test.sh
	#!/bin/sh
	echo "calling sleep \$TEST_ARG"
	echo "id=\$(id -u)"
	sleep \$TEST_ARG
	EOF
	sudo cp run-test.sh $confdir &&
	cleanup "sudo rm -f $confdr/run-test.sh" &&
	sudo chmod 755 $confdir/run-test.sh
'
test_expect_success HAVE_IMP 'flux exec --with-imp works' '
	TEST_ARG=0 sudo -u flux -E flux exec -vr 0 --with-imp test \
		>with-imp.out 2>&1 &&
	test_debug "cat with-imp.out" &&
	grep id=0 with-imp.out &&
	grep "calling sleep 0" with-imp.out
'
waitfile=$SHARNESS_TEST_SRCDIR/scripts/waitfile.lua

# Need to create a test output directory writable by flux user and
# readable by current user
test_expect_success HAVE_IMP 'create test output directory' '
	TESTDIR=$(mktemp --tmpdir=${TMPDIR:-/tmp} -d exec-with-impXXXX) &&
	cleanup "rm -rf $TESTDIR" &&
	chmod 777 $TESTDIR &&
	test_debug "echo created test directory $TESTDIR"
'
test_expect_success HAVE_IMP 'flux exec --with-imp forwards signals' '
	cat >test_signal.sh <<-EOF &&
	#!/bin/bash
        sig=\${1-INT}
        TEST_ARG=60 stdbuf --output=L flux exec --with-imp -v -n test \
                >${TESTDIR}/testready.out &
        $waitfile -vt 20 -p ^id=0 -c ${SIZE} ${TESTDIR}/testready.out &&
        kill -\$sig %1 &&
        wait %1
        exit \$?
	EOF
	chmod +x test_signal.sh &&
	test_expect_code 130 run_timeout 30 sudo -u flux ./test_signal.sh INT &&
	test_expect_code 143 run_timeout 30 sudo -u flux ./test_signal.sh TERM
'
test_expect_success HAVE_IMP 'flux exec flux-imp run forwards signals' '
	cat >test_signal.sh <<-EOF &&
	#!/bin/bash
        sig=\${1-INT}
        TEST_ARG=60 stdbuf --output=L flux exec -v -n $IMP run test \
                >${TESTDIR}/testready2.out &
        $waitfile -vt 20 -p ^id=0 -c ${SIZE} ${TESTDIR}/testready2.out &&
        kill -\$sig %1 &&
        wait %1
        exit \$?
	EOF
	chmod +x test_signal.sh &&
	test_expect_code 130 run_timeout 30 sudo -u flux ./test_signal.sh INT &&
	test_expect_code 143 run_timeout 30 sudo -u flux ./test_signal.sh TERM
'
