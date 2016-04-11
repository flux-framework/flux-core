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
	test_expect_code 142 run_timeout 1 sleep 2
'
test_expect_success 'test run_timeout with success' '
	run_timeout 1 /bin/true
'
test_expect_success 'we can find a flux binary' '
	flux --help >/dev/null
'
test_expect_success 'flux-keygen works' '
	umask 077 && tmpkeydir=`mktemp -d` &&
	flux --secdir $tmpkeydir keygen --force &&
	rm -rf $tmpkeydir
'
test_expect_success 'flux-start works' "
	flux start --size=2 'flux comms info' | grep 'size=2'
"
test_expect_success 'flux-start passes through errors from command' "
	test_must_fail flux start --size=1 /bin/false
"
test_expect_success 'flux-start passes exit code due to signal' "
	test_expect_code 130 flux start --size=1 'kill -INT \$\$'
"
test_expect_success 'test_under_flux works' '
	echo >&2 "$(pwd)" &&
	mkdir -p test-under-flux && (
		cd test-under-flux &&
		cat >.test.t <<-EOF &&
		#!$SHELL_PATH
		pwd
		test_description="test_under_flux (in sub sharness)"
		. "\$SHARNESS_TEST_SRCDIR"/sharness.sh
		test_under_flux 2
		test_expect_success "flux comms info" "
			flux comms info
		"
		test_done
		EOF
	chmod +x .test.t &&
	SHARNESS_TEST_DIRECTORY=`pwd` &&
	export SHARNESS_TEST_SRCDIR SHARNESS_TEST_DIRECTORY FLUX_BUILD_DIR debug &&
	./.test.t --verbose --debug >out 2>err
	) &&
	grep "size=2" test-under-flux/out
'
test_expect_success 'flux-start -o,--setattr ATTR=VAL can set broker attributes' '
	ATTR_VAL=`flux start -o,--setattr=foo-test=42 flux getattr foo-test` &&
	test $ATTR_VAL -eq 42
'
test_expect_success 'broker scratch-directory override works' '
	SCRATCHDIR=`mktemp -d` &&
	DIR=`flux start -o,--setattr=scratch-directory=$SCRATCHDIR flux getattr scratch-directory` &&
	test "$DIR" = "$SCRATCHDIR" &&
	test -d $SCRATCHDIR &&
	rmdir $SCRATCHDIR
'
test_expect_success 'broker persist-directory works' '
	PERSISTDIR=`mktemp -d` &&
	flux start -o,--setattr=persist-directory=$PERSISTDIR /bin/true &&
	test -d $PERSISTDIR &&
	test `ls -1 $PERSISTDIR|wc -l` -gt 0 &&
	rm -rf $PERSISTDIR
'
test_expect_success 'broker persist-filesystem works' '
	PERSISTFS=`mktemp -d` &&
	PERSISTDIR=`flux start -o,--setattr=persist-filesystem=$PERSISTFS flux getattr persist-directory` &&
	test -d $PERSISTDIR &&
	test `ls -1 $PERSISTDIR|wc -l` -gt 0 &&
	rm -rf $PERSISTDIR &&
	test -d $PERSISTFS &&
	rmdir $PERSISTFS
'
test_expect_success 'broker persist-filesystem is ignored if persist-directory set' '
	PERSISTFS=`mktemp -d` &&
	PERSISTDIR=`mktemp -d` &&
	DIR=`flux start -o,--setattr=persist-filesystem=$PERSISTFS,--setattr=persist-directory=$PERSISTDIR \
		flux getattr persist-directory` &&
	test "$DIR" = "$PERSISTDIR" &&
	test `ls -1 $PERSISTDIR|wc -l` -gt 0 &&
	rmdir $PERSISTFS &&
	rm -rf $PERSISTDIR
'
test_expect_success 'flux-help command list can be extended' '
	mkdir help.d &&
	cat <<-EOF  > help.d/test.json &&
	[{ "category": "test", "command": "test", "description": "a test" }]
	EOF
	cat <<-EOF  > help.expected &&
	Common commands from flux-test:
	   test               a test
	EOF
	FLUX_CMDHELP_PATTERN="help.d/*" flux help 2>&1 | sed "0,/^$/d" > help.out &&
	test_cmp help.expected help.out &&
	cat <<-EOF  > help.d/test2.json &&
	[{ "category": "test2", "command": "test2", "description": "a test two" }]
	EOF
	cat <<-EOF  >> help.expected &&

	Common commands from flux-test2:
	   test2              a test two
	EOF
	FLUX_CMDHELP_PATTERN="help.d/*" flux help 2>&1 | sed "0,/^$/d" > help.out &&
	test_cmp help.expected help.out
'
test_expect_success 'flux-help command can display manpages for subcommands' '
	PWD=$(pwd) &&
	cat <<-EOF >config &&
	general
	    man_path = ${PWD}/man
	EOF
	mkdir -p man/man1 &&
	cat <<-EOF > man/man1/flux-foo.1 &&
	.TH FOO "1" "January 1962" "Foo utils" "User Commands"
	.SH NAME
	foo \- foo bar baz
	EOF
	flux -c . help foo | grep "^FOO(1)"
'
test_expect_success 'flux-help returns nonzero exit code from man(1)' '
        man notacommand >/dev/null 2>&1
        code=$?
        test_expect_code $code flux help notacommand
'
test_expect_success 'builtin test_size_large () works' '
    size=$(test_size_large)  &&
    test -n "$size" &&
    size=$(FLUX_TEST_SIZE_MAX=2 test_size_large) &&
    test "$size" = "2" &&
    size=$(FLUX_TEST_SIZE_MIN=123 FLUX_TEST_SIZE_MAX=1000 test_size_large) &&
    test "$size" = "123"
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

test_done
