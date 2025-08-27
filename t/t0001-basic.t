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

# In some scenarios man(1) tests below may fail due to process sandboxing
# done via seccomp filter (e.g. for nix). Just disable seccomp for man for
# the duration of this test to avoid these failure
export MAN_DISABLE_SECCOMP=1

startctl=${SHARNESS_TEST_SRCDIR}/scripts/startctl.py
path_printenv=$(which printenv)

test_expect_success 'TEST_NAME is set' '
	test -n "$TEST_NAME"
'
test_expect_success_hd 'heredoc tests work: success' <<-'EOT'
	test -n "$TEST_NAME" &&
	# embedded heredoc
	cat >tmp.sh <<-EOF &&
	true
	EOF
	chmod +x tmp.sh &&
	./tmp.sh
EOT
test_expect_failure_hd 'heredoc tests work: failure' <<'EOT'
	test -n ""
EOT
test_expect_success 'run_timeout works' '
	test_expect_code 142 run_timeout -s ALRM 0.001 sleep 2
'
test_expect_success 'test run_timeout with success' '
	run_timeout 1 true
'
test_expect_success 'run_timeout fails if exec fails' '
	test_must_fail run_timeout 1 /nonexistent/executable
'
test_expect_success 'test_must_fail_or_be_terminated fails on success' '
	test_must_fail test_must_fail_or_be_terminated true
'
test_expect_success 'test_must_fail_or_be_terminated succeeds on nonzero exit' '
	test_must_fail_or_be_terminated false
'
test_expect_success 'test_must_fail_or_be_terminated succeeds on SIGTERM' '
	test_must_fail_or_be_terminated sh -c "kill \$\$"
'
test_expect_success 'test_must_fail_or_be_terminated fails on SIGHUP' '
	test_must_fail test_must_fail_or_be_terminated sh -c "kill -HUP \$\$"
'
test_expect_success 'we can find a flux binary' '
	flux --help >/dev/null
'
test_expect_success 'flux-keygen path argument is optional' '
	flux keygen
'
test_expect_success 'flux-keygen works' '
	flux keygen cert &&
	test -f cert
'
test_expect_success 'flux-keygen --name=test works' '
	flux keygen --name=testcert cert2 &&
	test -f cert2 &&
	grep testcert cert2
'
test_expect_success 'flux-keygen --meta works' '
	flux keygen --meta mammal=chinchilla,reptile=chamelion cert3 &&
	test -f cert3 &&
	grep mammal cert3 &&
	grep reptile cert3
'
test_expect_success 'flux-keygen --meta can update keygen.hostname' '
	flux keygen --meta=keygen.hostname=myhost cert4 &&
	test -f cert4 &&
	grep myhost cert4
'
test_expect_success 'flux-keygen --meta value can be an empty string' '
	flux keygen --meta smurf= cert5 &&
	test -f cert5 &&
	grep smurf cert5
'
test_expect_success 'flux-keygen --meta equal sign can be missing' '
	flux keygen --meta smurf cert6 &&
	test -f cert6 &&
	grep smurf cert6
'
test_expect_success 'flux-keygen fails with extra positional argument' '
	test_must_fail flux keygen cert xyz
'
test_expect_success 'flux-keygen generated cert with u=rw access' '
	echo '-rw-------' >cert-access.exp &&
	stat --format=%A cert >cert-access.out &&
	test_cmp cert-access.exp cert-access.out
'

test_expect_success 'flux-keygen overwrites existing cert' '
	test -f cert &&
	cp cert cert.bak &&
	flux keygen cert
'
test_expect_success 'flux-keygen generated a cert with different keys' '
	diff cert.bak cert | grep secret-key
'

test_expect_success 'flux-keygen fails with unknown arg' '
	test_must_fail flux keygen --force boguskey
'
test_expect_success 'flux-python command runs a python that finds flux' '
	flux python -c "import flux"
'

test_expect_success 'flux-python command runs the configured python' '
	define_line=$(grep "^#define PYTHON_INTERPRETER" ${FLUX_BUILD_DIR}/config/config.h) &&
	pypath=$(echo ${define_line} | sed -E '\''s~.*define PYTHON_INTERPRETER "(.*)"~\1~'\'') &&
	expected=$(${pypath} -c "import sys; print(sys.executable)") &&
	actual=$(flux python -c "import sys; print(sys.executable)") &&
	test "${expected}" = "${actual}"
'

test_expect_success 'flux fortune help works' '
	flux fortune --help | grep category
'

test_expect_success 'flux fortune works' '
	flux fortune
'

test_expect_success 'flux fortune all (default) works' '
	flux fortune -c all
'

test_expect_success 'flux fortune with valentine works' '
	flux fortune -c valentines
'

test_expect_success 'flux fortune with fun works' '
	flux fortune -c fun
'

test_expect_success 'flux fortune with facts works' '
	flux fortune -c facts
'

test_expect_success 'flux fortune with art works' '
	flux fortune -c art
'

# Minimal is sufficient for these tests, but test_under_flux unavailable
# clear the RC paths
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

test_expect_success 'flux-start in exec mode works' "
	flux start ${ARGS} flux getattr size | grep -x 1
"
test_expect_success 'flux-start in subprocess/pmi mode works (size 1)' "
	flux start ${ARGS} -s1 flux getattr size | grep -x 1
"
test_expect_success 'and broker.boot-method=simple' "
	test $(flux start ${ARGS} -s1 \
		flux getattr broker.boot-method) = "simple"
"
test_expect_success 'although method can be forced to single with attribute' "
	test $(flux start ${ARGS} -s1 -Sbroker.boot-method=single \
		flux getattr broker.boot-method) = "single"
"
test_expect_success 'or forced by setting FLUX_PMI_CLIENT_METHODS' "
	test $(FLUX_PMI_CLIENT_METHODS="single" flux start ${ARGS} -s1 \
		flux getattr broker.boot-method) = "single"
"
test_expect_success 'start fails when broker.boot-method=unknown' "
	test_must_fail flux start ${ARGS} -Sbroker.boot-method=unknown \
		true
"
test_expect_success 'flux-start in subprocess/pmi mode works (size 2)' "
	flux start ${ARGS} -s2 flux getattr size | grep -x 2
"
test_expect_success 'flux-start with size 1 has no peers' '
	echo 0 >nochild.exp &&
	flux start ${ARGS} -s1 \
		flux module stats --parse=child-count overlay >nochild.out &&
	test_cmp nochild.exp nochild.out
'
test_expect_success 'flux-start with size 2 has rank 1 peer' '
	echo 1 >child2.exp &&
	flux start ${ARGS} -s2 \
		flux module stats --parse=child-count overlay >child2.out &&
	test_cmp child2.exp child2.out
'
test_expect_success 'flux-start fails with unknown option' "
	test_must_fail flux start ${ARGS} --unknown true
"
test_expect_success 'flux-start fails with --verbose=badopt' "
	test_must_fail flux start ${ARGS} --verbose=badopt true
"
test_expect_success 'create bad broker shell script' '
	cat >flux-broker <<-EOT &&
	#!/badinterp
	EOT
	chmod +x flux-broker
'
test_expect_success 'flux-start exec fails on bad broker shell script' "
	test_must_fail bash -c 'FLUX_EXEC_PATH_PREPEND=. flux start true'
"
test_expect_success 'flux-start test exec fails on bad broker shell script' "
	#
	# We can't use test_must_fail here because on some OSes this command
	# might fail with exit code 127, which test_must_fail does not
	# accept, so we use ! here since any failure is acceptable here
	#
	! bash -c 'FLUX_EXEC_PATH_PREPEND=. flux start -s1 true'
"
test_expect_success 'flux-start -s1 works' "
	flux start ${ARGS} -s1 true
"
test_expect_success 'flux-start -s1 sets broker.mapping to expected value' "
	cat >mapping_1.exp <<-EOT &&
	[[0,1,1,1]]
	EOT
	flux start ${ARGS} -s1 flux getattr broker.mapping >mapping_1.out &&
	test_cmp mapping_1.exp mapping_1.out
"
test_expect_success 'flux-start --test-rundir without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-rundir=$(pwd) true
"
test_expect_success 'flux-start --test-pmi-clique without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-pmi-clique=none true
"
test_expect_success 'flux-start --test-hosts without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-hosts=foo true
"
test_expect_success 'flux-start --test-hosts with insufficient hosts fails' "
	test_must_fail flux start ${ARGS} -s2 --test-hosts=foo true
"
test_expect_success 'flux-start --test-hosts with garbled hosts fails' "
	test_must_fail flux start ${ARGS} -s2 --test-hosts=foo] true
"
test_expect_success 'flux-start --test-exit-timeout without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-exit-timeout=10s true
"
test_expect_success 'flux-start --test-exit-timeout fails with bad FSD' "
	test_must_fail flux start ${ARGS} -s1 --test-exit-timeout=-1 true
"
test_expect_success 'flux-start --test-exit-mode without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-exit-mode=any true
"
test_expect_success 'flux-start --test-exit-mode=leader is accepted' "
	flux start ${ARGS} -s1 --test-exit-mode=leader true
"
test_expect_success 'flux-start --test-exit-mode=badmode fails' "
	test_must_fail flux start ${ARGS} -s1 --test-exit-mode=badmode true
"
test_expect_success 'flux-start --test-start-mode without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-start-mode=all true
"
test_expect_success 'flux-start --test-start-mode=all is accepted' "
	flux start ${ARGS} -s1 --test-start-mode=all true
"
test_expect_success 'flux-start --test-start-mode=badmode fails' "
	test_must_fail flux start ${ARGS} -s1 --test-start-mode=badmode true
"
test_expect_success 'flux-start --test-rundir-cleanup without --test-size fails' "
	test_must_fail flux start ${ARGS} --test-rundir-cleanup true
"
test_expect_success 'flux-start --verbose=2 enables PMI tracing' "
	flux start ${ARGS} \
		--test-size=1 --verbose=2 \
		true 2>&1 | grep pmi_version
"
test_expect_success 'flux-start -vv also does' "
	flux start ${ARGS} \
		--test-size=1 -vv \
		true 2>&1 | grep pmi_version
"
test_expect_success 'flux-start --test-pmi-clique=single works' "
	flux start ${ARGS} \
		--test-size=1 \
		--test-pmi-clique=single \
		true
"
test_expect_success 'flux-start --test-pmi-clique=none works' "
	flux start ${ARGS} --test-size=1 \
		--test-pmi-clique=single \
		true
"
test_expect_success 'flux-start --test-pmi-clique=badmode fails' "
	test_must_fail flux start ${ARGS} --test-size=1 \
		--test-pmi-clique=badmode \
		true
"
test_expect_success 'flux-start embedded server works from initial program' "
	flux start -v ${ARGS} -s1 flux python ${startctl} status \
		>startctl.out 2>startctl.err
"
test_expect_success 'flux-start embedded server status got JSON' "
	jq -c . <startctl.out
"
test_expect_success 'flux-start embedded server logs hi/bye from client' "
	grep hi startctl.err &&
	grep bye startctl.err
"
test_expect_success 'flux-start embedded server logs disconnect notification' "
	grep 'disconnect from' startctl.err
"
test_expect_success 'flux-start in exec mode passes through errors from command' "
	test_must_fail flux start ${ARGS} false
"
test_expect_success 'flux-start in subprocess/pmi mode passes through errors from command' "
	test_must_fail flux start ${ARGS} -s1 false
"
test_expect_success 'flux-start in exec mode passes exit code due to signal' "
	test_expect_code 130 flux start ${ARGS} 'kill -INT \$\$'
"
test_expect_success 'flux-start in subprocess/pmi mode passes exit code due to signal' "
	test_expect_code 130 flux start ${ARGS} -s1 'kill -INT \$\$'
"
test_expect_success 'flux-start in exec mode works as initial program' "
	flux start ${ARGS} -s2 flux start ${ARGS} flux getattr size | grep -x 1
"
test_expect_success 'flux-start in subprocess/pmi mode works as initial program' "
	flux start ${ARGS} -s2 flux start ${ARGS} -s1 flux getattr size | grep -x 1
"

# See issue #5447: rc1 can fail if the shell sources a script that reactivates
# errexit mode and has an unchecked error.  'command' apparently reactivates
# errexit mode in older versions of bash.
#
test_expect_success 'flux-start works with non-errexit clean BASH_ENV' '
	cat >testbashrc <<-EOT &&
	command -v ls >/dev/null || :
	false
	true
	EOT
	BASH_ENV=testbashrc flux start true
'

test_expect_success 'flux-start works with multiple files in rc1.d' '
	mkdir -p rc1.d &&
	printf "#!/bin/sh\necho rc-one\n" >rc1.d/one &&
	printf "#!/bin/sh\necho rc-two\n" >rc1.d/two &&
	chmod +x rc1.d/* &&
	FLUX_RC_EXTRA=$(pwd) flux start -Slog-stderr-level=6 \
		echo rc-three >rc-multi.out 2>&1 &&
	test_debug "cat rc-multi.out" &&
	grep rc-one rc-multi.out &&
	grep rc-two rc-multi.out &&
	grep rc-three rc-multi.out
'

test_expect_success 'flux-start works with zero files in rc1.d' '
	rm rc1.d/* &&
	FLUX_RC_EXTRA=$(pwd) flux start echo rc-done >rc-zero.out 2>&1 &&
	test_debug "cat rc-zero.out" &&
	grep rc-done rc-zero.out
'

test_expect_success 'flux-start --wrap option works' '
	broker_path=$(flux start ${ARGS} -vX 2>&1 | sed "s/^flux-start: *//g") &&
	echo broker_path=${broker_path} &&
	test -n "${broker_path}" &&
	flux start ${ARGS} --wrap=/bin/echo,start: arg0 arg1 arg2 > wrap.output &&
	test_debug "cat wrap.output" &&
	cat >wrap.expected <<-EOF &&
	start: ${broker_path} arg0 arg1 arg2
	EOF
	test_cmp wrap.expected wrap.output
'
test_expect_success 'flux-start --wrap option works with --test-size' '
	flux start ${ARGS} -s2 -vX --wrap=test-wrap > wrap2.output 2>&1 &&
	test_debug "cat wrap2.output" &&
	test "$(grep -c test-wrap wrap2.output)" = "2"
'

test_expect_success 'flux-start dies gracefully when run from removed dir' '
	mkdir foo && (
	 cd foo &&
	 rmdir ../foo &&
	 test_must_fail flux start true )
'

command -v hwloc-ls >/dev/null && test_set_prereq HWLOC_LS
test_expect_success HWLOC_LS 'FLUX_HWLOC_XMLFILE works' '
	hwloc-ls --of xml -i "numa:2 core:3 pu:1" >test.xml &&
	cat <<-EOF >norestrict.conf &&
	[resource]
	norestrict = true
	EOF
	FLUX_HWLOC_XMLFILE=test.xml \
		flux start -s2 --conf=norestrict.conf \
			flux resource info >rinfo.out &&
	test_debug "cat rinfo.out" &&
	grep "12 Cores" rinfo.out
'

# too slow under ASAN
test_expect_success NO_ASAN 'test_under_flux works' '
	echo >&2 "$(pwd)" &&
	mkdir -p test-under-flux && (
	cd test-under-flux &&
	SHARNESS_TEST_DIRECTORY=`pwd` &&
	export SHARNESS_TEST_SRCDIR SHARNESS_TEST_DIRECTORY FLUX_BUILD_DIR debug &&
	run_timeout 10 "$SHARNESS_TEST_SRCDIR"/test-under-flux/test.t --verbose --debug >out 2>err
	) &&
	grep -x "2" test-under-flux/out
'

test_expect_success NO_ASAN 'test_under_flux fails if loaded modules are not unloaded' '
	mkdir -p test-under-flux && (
	cd test-under-flux &&
	SHARNESS_TEST_DIRECTORY=`pwd` &&
	export SHARNESS_TEST_SRCDIR SHARNESS_TEST_DIRECTORY FLUX_BUILD_DIR debug &&
	unset verbose &&
	test_expect_code 1 "$SHARNESS_TEST_SRCDIR"/test-under-flux/t_modcheck.t 2>err.modcheck \
			| grep -v sharness: >out.modcheck
	) &&
	test_cmp "$SHARNESS_TEST_SRCDIR"/test-under-flux/expected.modcheck test-under-flux/out.modcheck
'

test_expect_success 'flux-start -o,--setattr ATTR=VAL can set broker attributes' '
	ATTR_VAL=`flux start ${ARGS} -o,--setattr=foo-test=42 flux getattr foo-test` &&
	test $ATTR_VAL -eq 42
'
test_expect_success 'flux-start --setattr ATTR=VAL can set broker attributes' '
	ATTR_VAL=`flux start ${ARGS} --setattr=foo-test=42 flux getattr foo-test` &&
	test $ATTR_VAL -eq 42
'
test_expect_success 'hostlist attr is set on size 1 instance' '
	hn=$(hostname) &&
	cat >hostlist1.exp <<-EOT &&
	$hn
	EOT
	flux start ${ARGS} flux exec flux getattr hostlist >hostlist1.out &&
	test_cmp hostlist1.exp hostlist1.out
'
test_expect_success 'hostlist attr is set on all ranks of size 4 instance' '
	flux start ${ARGS} -s4 flux exec flux getattr hostlist
'
test_expect_success 'flux start (singleton) cleans up rundir' '
	flux start ${ARGS} \
		flux getattr rundir >rundir_pmi.out &&
	RUNDIR=$(cat rundir_pmi.out) &&
	test_must_fail test -d $RUNDIR
'
test_expect_success 'flux start -s1 cleans up rundirs' '
	flux start ${ARGS} -s1 \
		flux getattr rundir >rundir_selfpmi1.out &&
	RUNDIR=$(cat rundir_selfpmi1.out) &&
	test -n "$RUNDIR" &&
	test_must_fail test -d $RUNDIR
'
test_expect_success 'flux start -s2 cleans up rundirs' '
	flux start ${ARGS} -s2 \
		flux getattr rundir >rundir_selfpmi2.out &&
	RUNDIR=$(cat rundir_selfpmi2.out) &&
	test -n "$RUNDIR" &&
	test_must_fail test -d $RUNDIR
'
test_expect_success 'flux start --test-rundir works' '
	RUNDIR=$(mktemp -d) &&
	flux start ${ARGS} --test-size=1 \
		--test-rundir=$RUNDIR \
		flux getattr rundir >rundir_test.out &&
	echo $RUNDIR >rundir_test.exp &&
	test_cmp rundir_test.exp rundir_test.out &&
	rmdir $RUNDIR
'
test_expect_success 'flux start --test-rundir --test-rundir-cleanup works' '
	RUNDIR=$(mktemp -d) &&
	flux start ${ARGS} --test-size=1 \
		--test-rundir=$RUNDIR \
		--test-rundir-cleanup \
		flux getattr rundir >rundir_test.out &&
	echo $RUNDIR >rundir_test.exp &&
	test_cmp rundir_test.exp rundir_test.out &&
	test_must_fail test -d $RUNDIR
'
test_expect_success 'flux start --test-rundir with missing directory fails' '
	test_must_fail flux start ${ARGS} --test-size=1 \
		--test-rundir=/noexist \
		true 2>noexist_rundir.err &&
	grep "/noexist: No such file or directory" noexist_rundir.err
'
test_expect_success 'flux start --test-rundir with not-directory fails' '
	touch notdir &&
	test_must_fail flux start ${ARGS} --test-size=1 \
		--test-rundir=notdir \
		true 2>notdir_rundir.err &&
	grep "notdir: not a directory" notdir_rundir.err
'
test_expect_success 'rundir override works' '
	RUNDIR=`mktemp -d` &&
	DIR=`flux start ${ARGS} --setattr=rundir=$RUNDIR flux getattr rundir` &&
	test "$DIR" = "$RUNDIR" &&
	test -d $RUNDIR &&
	rm -rf $RUNDIR
'
test_expect_success 'rundir override creates nonexistent dirs and cleans up' '
	RUNDIR=`mktemp -d` &&
	rmdir $RUNDIR &&
	flux start ${ARGS} --setattr=rundir=$RUNDIR sh -c "test -d $RUNDIR" &&
	test_expect_code 1 test -d $RUNDIR
'
test_expect_success 'broker fails gracefully when rundir buffer overflows' '
	longstring=$(head -c 1024 < /dev/zero | tr \\0 D) &&
	! TMPDIR=$longstring flux start ${ARGS} true 2>overflow.err &&
	grep overflow overflow.err
'
test_expect_success 'broker fails gracefully on nonexistent TMPDIR' '
	! TMPDIR=/noexist flux start ${ARGS} true 2>noexist.err &&
	grep "cannot create directory in /noexist" noexist.err
'
test_expect_success 'broker fails gracefully on non-directory rundir' '
	touch notdir &&
	test_must_fail flux start ${ARGS} -Srundir=notdir \
		true 2>notdir.err &&
	grep "Not a directory" notdir.err
'
test_expect_success 'broker fails gracefully on unwriteable rundir' '
	mkdir -p privdir &&
	chmod u-w privdir &&
	test_must_fail flux start ${ARGS} -Srundir=privdir \
		true 2>privdir.err &&
	grep "permissions" privdir.err
'
# statedir created here is reused in the next several tests
test_expect_success 'broker statedir is not cleaned up' '
	mkdir -p statedir &&
	flux start ${ARGS} -Sstatedir=$(pwd)/statedir true &&
	test -d statedir
'
test_expect_success 'broker statedir cannot be changed at runtime' '
	test_must_fail flux start ${ARGS} -Sstatedir=$(pwd)/statedir \
		flux setattr statedir $(pwd)/statedir 2>rostatedir.err &&
	grep "Operation not permitted" rostatedir.err
'
test_expect_success 'broker statedir cannot be set at runtime' '
	test_must_fail flux start ${ARGS} \
		flux setattr statedir $(pwd)/statedir 2>rostatedir2.err &&
	grep "Operation not permitted" rostatedir2.err
'
test_expect_success 'broker fails when statedir does not exist' '
	rm -rf statedir &&
	test_must_fail flux start ${ARGS} -Sstatedir=$(pwd)/statedir \
		true 2>nostatedir.err &&
	grep "cannot stat" nostatedir.err
'
# Use -eq hack to test that BROKERPID is a number
test_expect_success 'broker broker.pid attribute is readable' '
	BROKERPID=`flux start ${ARGS} flux getattr broker.pid` &&
	test -n "$BROKERPID" &&
	test "$BROKERPID" -eq "$BROKERPID"
'
test_expect_success 'broker sets parent-uri attribute only for jobs' '
	flux start flux run flux start flux getattr parent-uri &&
	test_must_fail \
		flux start flux run flux start -s1 flux getattr parent-uri
'
test_expect_success 'broker sets parent-kvs-namespace attribute only for jobs' '
	flux start flux run flux start flux getattr parent-kvs-namespace &&
	test_must_fail \
		flux start flux run \
			flux start -s1 flux getattr parent-kvs-namespace
'
test_expect_success 'local-uri override works' '
	sockdir=$(mktemp -d) &&
	newsock=local://$sockdir/meep &&
	echo $newsock >uri.exp &&
	flux start ${ARGS} \
		-Slocal-uri=$newsock \
		printenv FLUX_URI >uri.out &&
	test_cmp uri.exp uri.out &&
	rm -rf $sockdir
'
test_expect_success 'broker fails gracefully when local-uri is malformed' '
	test_must_fail flux start ${ARGS} -Slocal-uri=baduri \
		true 2>baduri.err &&
	grep malformed baduri.err
'
test_expect_success 'broker fails gracefully when local-uri buffer overflows' '
	longuri="local://$(head -c 1024 < /dev/zero | tr \\0 D)" &&
	test_must_fail flux start ${ARGS} -Slocal-uri=${longuri} \
		true 2>longuri.err &&
	grep "buffer overflow" longuri.err
'
test_expect_success 'broker fails gracefully when local-uri in missing dir' '
	test_must_fail flux start ${ARGS} -Slocal-uri=local:///noexist/x \
		true 2>nodiruri.err &&
	grep "cannot stat" nodiruri.err
'
test_expect_success 'broker fails gracefully when local-uri in non-dir' '
	touch urinotdir &&
	test_must_fail flux start ${ARGS} \
		-Slocal-uri=local://$(pwd)/urinotdir/x \
		true 2>urinotdir.err &&
	grep "Not a directory" urinotdir.err
'
test_expect_success 'broker fails gracefully when local-uri in unwritable dir' '
	mkdir -p privdir &&
	chmod u-w privdir &&
	test_must_fail flux start ${ARGS} \
		-Slocal-uri=local://$(pwd)/privdir/x \
		true 2>uriprivdir.err &&
	grep "permissions" uriprivdir.err
'
test_expect_success 'broker broker.pid attribute is immutable' '
	test_must_fail flux start ${ARGS} --setattr=broker.pid=1234 flux getattr broker.pid
'
test_expect_success 'broker --verbose option works' '
	flux start ${ARGS} -o,-v true
'
test_expect_success 'broker fails on invalid broker.critical-ranks option' '
	test_must_fail flux start ${ARGS} -Sbroker.critical-ranks=0-1
'
test_expect_success 'broker fails on unknown option' '
	test_must_fail flux start ${ARGS} -o,--not-an-option true
'
test_expect_success 'flux-help command list can be extended' '
	mkdir help.d &&
	cat <<-EOF  > help.d/test.json &&
	[{ "name": "test", "description": "test commands",
	 "commands": [ {"name": "test", "description": "a test" }]}]
	EOF
	FLUX_CMDHELP_PATTERN="help.d/*" flux help > help.out 2>&1 &&
	grep "^test commands" help.out &&
	grep "a test" help.out &&
	cat <<-EOF  > help.d/test2.json &&
	[{ "name": "test", "description": "test two commands",
	 "commands": [ {"name": "test2", "description": "a test two"}]}]
	EOF
	FLUX_CMDHELP_PATTERN="help.d/*" flux help > help2.out 2>&1 &&
	grep "^test two commands" help2.out &&
	grep "a test two" help2.out
'
command -v man >/dev/null && test_set_prereq HAVE_MAN
test_expect_success HAVE_MAN 'flux-help command can display manpages for subcommands' '
	PWD=$(pwd) &&
	mkdir -p man/man1 &&
	cat <<-EOF > man/man1/flux-foo.1 &&
	.TH FOO "1" "January 1962" "Foo utils" "User Commands"
	.SH NAME
	foo \- foo bar baz
	EOF
	MANPATH=${PWD}/man FLUX_IGNORE_NO_DOCS=y flux help foo | grep "^FOO(1)"
'
test_expect_success HAVE_MAN 'flux-help command can display manpages for api calls' '
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
test_expect_success HAVE_MAN 'flux-help returns nonzero exit code from man(1)' '
	test_expect_code $(missing_man_code) \
		eval FLUX_IGNORE_NO_DOCS=y flux help notacommand
'
test_expect_success 'flux appends colon to missing or unset MANPATH' '
	(unset MANPATH && flux $path_printenv | grep "MANPATH=.*:$") &&
	MANPATH= flux $path_printenv | grep "MANPATH=.*:$"
'
test_expect_success 'flux deduplicates FLUX_RC_EXTRA & FLUX_SHELL_RC_PATH' '
	FLUX_RC_EXTRA=/foo:/bar:/foo \
		flux $path_printenv FLUX_RC_EXTRA | grep "^/foo:/bar$" &&
	FLUX_SHELL_RC_PATH=/foo:/bar:/foo \
		flux $path_printenv FLUX_SHELL_RC_PATH | grep "^/foo:/bar$"
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
	flux start ${ARGS} $waitfile -v -t 5 -p "hello" waitfile.test.1 &
	p=$! &&
	echo "hello" > waitfile.test.1 &&
	wait $p
'

test_expect_success 'scripts/waitfile works after <1s' '
	flux start ${ARGS} $waitfile -v -t 2 -p "hello" -P- waitfile.test.2 <<-EOF &
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
	flux start ${ARGS} $waitfile -v -t 5 -p "hello" -P- waitfile.test.3 <<-EOF &
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
	flux start ${ARGS} -s2 bash -c "nohup flux event sub heartbeat.pulse &"
'

# test for issue #1191
test_expect_success 'passing NULL to flux_log functions logs to stderr (#1191)' '
	${FLUX_BUILD_DIR}/t/loop/logstderr > std.out 2> std.err &&
	grep "warning: hello" std.err &&
	grep "err: world: No such file or directory" std.err
'

# tests for issue #3925
test_expect_success 'setting rundir to a long directory fails (#3925)' '
	longdir=rundir-01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789 &&
	mkdir -p $longdir &&
	test_must_fail flux start ${ARGS} \
		-Srundir=$longdir \
		true 2>longrun.err &&
	grep "exceeds max" longrun.err
'

test_expect_success 'setting local-uri to a long path fails (#3925)' '
	longdir=rundir-01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789 &&
	mkdir -p $longdir &&
	test_must_fail flux start ${ARGS} \
		-Slocal-uri=local://$longdir/local-0 \
		true 2>longuri.err &&
	grep "exceeds max" longuri.err
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

test_expect_success 'no unit tests built with libtool wrapper' '
	find ${FLUX_BUILD_DIR} \
		-name "test_*.t" \
		-type f \
		-executable \
		-printf "%h\n" \
		| uniq \
		| xargs -i basename {} > test_dirs &&
	test_debug "cat test_dirs" &&
	test_must_fail grep -q "\.libs" test_dirs
'

CMDS="\
R \
admin \
cron \
event \
exec \
job \
jobs \
jobtap \
keygen \
kvs \
logger \
module \
ping \
queue \
resource \
start \
terminus \
"

test_cmd_help ()
{
	local rc=0
	for cmd in ${CMDS}; do
		flux ${cmd} --help 2>&1 | grep -i usage || rc=1
	done
	return ${rc}
}

KVS_SUBCMDS="\
namespace \
eventlog \
"

test_kvs_subcmd_help ()
{
	local rc=0
	for subcmd in ${KVS_SUBCMDS}; do
		flux kvs ${subcmd} --help 2>&1 | grep -i usage || rc=1
	done
	return ${rc}
}

test_expect_success 'command --help works outside of flux instance' '
	flux --help 2>&1 | grep -i usage &&
	test_cmd_help &&
	test_kvs_subcmd_help
'

# Note: flux-start auto-removes rundir

test_done
