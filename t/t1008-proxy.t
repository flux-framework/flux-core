#!/bin/sh
#

test_description='Test ssh:// connector and flux-proxy'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

export TEST_URI=$FLUX_URI
export TEST_JOBID=$(flux getattr session-id)
export TEST_SOCKDIR=$(echo $FLUX_URI | sed -e "s!local://!!") &&
export TEST_FLUX=${FLUX_BUILD_DIR}/src/cmd/flux
export TEST_TMPDIR=${TMPDIR:-/tmp}
unset FLUX_URI

export TEST_SSH=${SHARNESS_TEST_SRCDIR}/scripts/tssh

test_expect_success 'flux-proxy creates new socket' '
	PROXY_URI=$(flux proxy $TEST_URI printenv FLUX_URI) &&
	test "$PROXY_URI" != "$TEST_URI"
'

test_expect_success 'flux-proxy cleans up socket' '
	PROXY_URI=$(flux proxy $TEST_URI printenv FLUX_URI) &&
	path=$(echo $PROXY_URI | sed -e "s!local://!!") &&
	! test -d $path
'

test_expect_success 'flux-proxy exits with command return code' '
	flux proxy $TEST_URI /bin/true &&
	! flux proxy $TEST_URI /bin/false
'

test_expect_success 'flux-proxy forwards getattr request' '
	ATTR_SIZE=$(flux proxy $TEST_URI flux getattr size) &&
	test "$ATTR_SIZE" = "$SIZE"
'

test_expect_success 'flux-proxy JOB works with session-id' '
	flux proxy $TEST_JOBID /bin/true
'

test_expect_success 'flux-proxy manages event redistribution' '
	flux proxy $TEST_JOBID \
	  "flux event sub -c1 hb& flux event sub -c1 hb& wait;wait" &&
	FLUX_URI=$TEST_URI flux dmesg | sed -e "s/[^ ]* //" >event.out &&
	test $(egrep "connector-local.*debug\[0\]: subscribe hb" event.out|wc -l) -eq 1 &&
	test $(egrep "proxy.*debug\[0\]: subscribe hb" event.out|wc -l) -eq 1 &&
	test $(egrep "connector-local.*debug\[0\]: unsubscribe hb" event.out|wc -l) -eq 1 &&
	test $(egrep "proxy.*debug\[0\]: unsubscribe hb" event.out|wc -l) -eq 1
'

test_expect_success 'flux-proxy --setenv option works' '
	TVAL=$(flux proxy --setenv TVAR=xxx $TEST_URI printenv TVAR) &&
	test "$TVAL" = "xxx"
'

test_expect_success 'ssh:// with local sockdir works' '
	FLUX_URI=ssh://localhost${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size
'

test_expect_success 'ssh:// with local sockdir and port works' '
	FLUX_URI=ssh://localhost:42${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size
'

test_expect_success 'ssh:// with local sockdir and user works' '
	FLUX_URI=ssh://fred@localhost${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size
'

test_expect_success 'ssh:// with local sockdir, user, and port works' '
	FLUX_URI=ssh://fred@localhost:42${TEST_SOCKDIR} FLUX_SSH=$TEST_SSH \
	  flux getattr size
'

test_expect_success 'ssh:// with jobid works' '
	FLUX_URI=ssh://localhost/$TEST_JOBID FLUX_SSH=$TEST_SSH \
	  flux getattr size
'

test_expect_success 'ssh:// can handle nontrivial message load' '
	FLUX_URI=ssh://localhost/$TEST_JOBID FLUX_SSH=$TEST_SSH \
	  flux kvs dir -R >dir.out
'

test_expect_success 'ssh:// can work with events' '
	FLUX_URI=ssh://localhost/$TEST_JOBID FLUX_SSH=$TEST_SSH \
	  flux event sub --count=1 hb
'

test_expect_success 'ssh:// with bad query option fails in flux_open()' '
	FLUX_URI=ssh://localhost/$TEST_JOBID?badarg=bar FLUX_SSH=$TEST_SSH \
	  flux getattr size 2>badarg.out; test $? -ne 0 &&
	grep -q "flux_open:" badarg.out
'

test_expect_success 'ssh:// with bad FLUX_SSH value fails in flux_open()' '
	FLUX_URI=ssh://localhost/$TEST_JOBID FLUX_SSH=/noexist \
	  flux getattr size 2>noexist.out; test $? -ne 0 &&
	grep -q "flux_open:" noexist.out
'

test_expect_success 'ssh:// with bad FLUX_SSH_RCMD value fails in flux_open()' '
	FLUX_URI=ssh://localhost/$TEST_JOBID FLUX_SSH=$TEST_SSH \
	  FLUX_SSH_RCMD=/nocmd flux getattr size 2>nocmd.out; test $? -ne 0 &&
	grep -q "flux_open:" nocmd.out
'

test_expect_success 'ssh:// with missing path component fails in flux_open()' '
	FLUX_URI=ssh://localhost FLUX_SSH=$TEST_SSH \
	  flux getattr size 2>nopath.out; test $? -ne 0 &&
	grep -q "flux_open:" nopath.out
'

test_expect_success 'flux proxy works with ssh:// and jobid' '
	FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy ssh://localhost/$TEST_JOBID flux getattr size
'

test_expect_success 'flux proxy works with ssh:// and local sockdir' '
	FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy ssh://localhost/${TEST_SOCKDIR} flux getattr size
'

test_expect_success 'flux proxy with ssh:// and bad jobid fails' '
	! FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy ssh://localhost/noexist flux getattr size
'

test_expect_success 'flux proxy with ssh:// and bad query option fails' '
	! FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy "ssh://localhost/${TEST_JOBID}?badarg=bar" \
	    flux getattr size
'

test_expect_success 'flux proxy with ssh:// and TMPDIR query option works' '
        XURI="ssh://localhost/${TEST_JOBID}?setenv=TMPDIR=$TEST_TMPDIR" &&
	FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy $XURI flux getattr size
'

test_expect_success 'flux proxy with ssh:// and wrong TMPDIR query option fails' '
	! FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy "ssh://localhost/${TEST_JOBID}?setenv=TMPDIR=/nope" \
	    flux getattr size
'

test_expect_success 'flux proxy with ssh:// and two env query option works' '
        XURI="ssh://localhost/${TEST_JOBID}?setenv=TMPDIR=$TEST_TMPDIR&setenv=FOO=xyz" &&
	FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy $XURI flux getattr size
'

test_expect_success 'flux proxy with ssh:// and second bad query option fails' '
        XURI="ssh://localhost/${TEST_JOBID}?setenv=TMPDIR=$TEST_TMPDIR&badarg=bar" &&
	! FLUX_SSH=$TEST_SSH FLUX_SSH_RCMD=$TEST_FLUX \
	  flux proxy $XURI flux getattr size
'

test_done
