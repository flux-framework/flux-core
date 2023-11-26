#!/bin/sh
#

test_description='Test ssh:// connector and flux-relay'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

export TEST_SOCKDIR=$(echo $FLUX_URI | sed -e "s!local://!!") &&

export TEST_SSH=${SHARNESS_TEST_SRCDIR}/scripts/tssh
RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'load heartbeat module with fast rate' '
        flux module load heartbeat period=0.1s
'
test_expect_success 'ssh:// with local sockdir works' '
	FLUX_URI=ssh://localhost${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size 2>basic.err &&
	  grep hostname=localhost basic.err &&
	  grep cmd= basic.err | grep "flux relay $TEST_SOCKDIR"
'
test -x /bin/tcsh && test_set_prereq HAVE_TCSH
test_expect_success HAVE_TCSH 'ssh:// with local sockdir and SHELL=tcsh works' '
	FLUX_URI=ssh://localhost${TEST_SOCKDIR} \
	FLUX_SSH=$TEST_SSH \
	SHELL=/bin/tcsh \
	  flux getattr size 2>basic.err &&
	  grep hostname=localhost basic.err &&
	  grep cmd= basic.err | grep "flux relay $TEST_SOCKDIR"
'

test_expect_success 'ssh:// with local sockdir and port works' '
	FLUX_URI=ssh://localhost:42${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size 2>basicp.err &&
	  grep hostname=localhost basicp.err &&
	  grep port=42 basicp.err &&
	  grep cmd= basicp.err | grep "flux relay $TEST_SOCKDIR"
'

test_expect_success 'ssh:// with local sockdir and user works' '
	FLUX_URI=ssh://fred@localhost${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size 2>basicu.err &&
	  grep hostname=fred@localhost basicu.err &&
	  grep cmd= basicu.err | grep "flux relay $TEST_SOCKDIR"
'

test_expect_success 'ssh:// with local sockdir, user, and port works' '
	FLUX_URI=ssh://fred@localhost:42${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size 2>basicpu.err &&
	  grep port=42 basicpu.err &&
	  grep hostname=fred@localhost basicpu.err &&
	  grep cmd= basicpu.err | grep "flux relay $TEST_SOCKDIR"

'

test_expect_success 'ssh:// can handle nontrivial message load' '
	FLUX_URI=ssh://localhost$TEST_SOCKDIR FLUX_SSH=$TEST_SSH \
	  flux ping --count=100 -i 0.001 broker >/dev/null
'

test_expect_success 'ssh:// can work with events' '
	FLUX_URI=ssh://localhost$TEST_SOCKDIR FLUX_SSH=$TEST_SSH \
	  flux event sub --count=1 heartbeat.pulse
'

test_expect_success 'ssh:// can register a service' "
	FLUX_URI=ssh://localhost$TEST_SOCKDIR FLUX_SSH=$TEST_SSH \
	  echo '{\"service\":\"fubar\"}' >service.add.in &&
	  $RPC service.add 0 <service.add.in
"

test_expect_success 'ssh:// can re-register a service after disconnect' "
	FLUX_URI=ssh://localhost$TEST_SOCKDIR FLUX_SSH=$TEST_SSH \
	  echo '{\"service\":\"fubar\"}' >service2.add.in &&
	  $RPC service.add 0 <service2.add.in
"

test_expect_success 'ssh:// cannot register a service with method (EINVAL)' "
	FLUX_URI=ssh://localhost$TEST_SOCKDIR FLUX_SSH=$TEST_SSH \
	  echo '{\"service\":\"fubar.baz\"}' >service3.add.in &&
	  $RPC service.add 22 <service3.add.in
"

test_expect_success 'ssh:// cannot shadow a broker service (EEXIST)' "
	FLUX_URI=ssh://localhost$TEST_SOCKDIR FLUX_SSH=$TEST_SSH \
	  echo '{\"service\":\"broker\"}' >service4.add.in &&
	  $RPC service.add 17 <service4.add.in
"

test_expect_success 'ssh:// with bad query option fails in flux_open()' '
	test_must_fail env \
	    FLUX_URI=ssh://localhost$TEST_SOCKDIR?badarg=bar \
	    FLUX_SSH=$TEST_SSH \
	    flux getattr size 2>badarg.out &&
	grep -q "flux_open:" badarg.out
'

test_expect_success 'ssh:// with bad FLUX_SSH value fails in flux_open()' '
	test_must_fail env \
	    FLUX_URI=ssh://localhost$TEST_SOCKDIR \
	    FLUX_SSH=/noexist \
	    flux getattr size 2>noexist.out &&
	grep -q "flux_open:" noexist.out
'

test_expect_success 'ssh:// with bad FLUX_SSH_RCMD value fails in flux_open()' '
	test_must_fail env \
	    FLUX_URI=ssh://localhost$TEST_SOCKDIR \
	    FLUX_SSH=$TEST_SSH \
	    FLUX_SSH_RCMD=/nocmd \
	    flux getattr size 2>nocmd.out &&
	grep -q "flux_open:" nocmd.out
'

test_expect_success 'ssh:// with missing path component fails in flux_open()' '
	test_must_fail env \
	    FLUX_URI=ssh://localhost \
	    FLUX_SSH=$TEST_SSH \
	    flux getattr size 2>nopath.out &&
	grep -q "flux_open:" nopath.out
'

test_expect_success 'create test ssh that emits errors on stderr' '
	cat <<-EOF >ssh.sh &&
	#!/bin/sh
	printf "error from ssh\n" >&2
	exit 1
	EOF
	chmod +x ssh.sh
'

test_expect_success 'ssh:// stderr is redirected with Python flux.Flux()' '
	cat <<-EOF >open_ex.py &&
	import os
	import flux
	try:
	    flux.Flux()
	except OSError as err:
	    print (f"Exception: errno={err.errno} msg={err.strerror}")
	EOF
	cat <<-EOF2 >open_ex.expected &&
	Exception: errno=104 msg=Unable to connect to Flux: error from ssh
	EOF2
	FLUX_URI=ssh://localhost/foo/bar FLUX_SSH=$(pwd)/ssh.sh \
	  flux python open_ex.py >open_ex.out 2>open_ex.err &&
	test_must_be_empty open_ex.err &&
	test_cmp open_ex.expected open_ex.out
'

test_expect_success 'remove heartbeat module' '
        flux module remove heartbeat
'

test_done
