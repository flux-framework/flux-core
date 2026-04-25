#!/bin/sh
#
# ci=system

test_description='Test exec command --jobid option

Test exec --jobid functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} job

if flux job submit --help 2>&1 | grep -q sign-type; then
	test_set_prereq FLUX_SECURITY
	# flux-core is built with flux-security, but ensure sign_wrap() works
	# in this environment (configured sign-type may not be functional)
	flux python -c \
	 'from flux.security import SecurityContext as C;C().sign_wrap("foo")' \
	   >/dev/null 2>&1 \
	   && test_set_prereq SIGN_WRAP
fi

TMPDIR=$(cd /tmp && $(which pwd))

test_expect_success 'flux exec --jobid fails with invalid job id' '
	test_expect_code 1 flux exec --jobid=f-o-o true 2>badid.err &&
	test_debug "cat badid.err" &&
	grep "error parsing jobid" badid.err
'
test_expect_success 'flux exec --jobid fails for nonexistent job id' '
	test_expect_code 1 flux exec --jobid=f1 true 2>noid.err &&
	test_debug "cat noid.err" &&
	grep "not found" noid.err
'
test_expect_success 'run two jobs on different ranks' '
	flux submit --wait-event=start --bcc=1-2 -N1 sleep inf &&
	id1=$(flux job last [-1:]) &&
	id2=$(flux job last) &&
	test_debug "flux jobs -no \"{id} {ranks}\""
'
test_expect_success 'flux exec --jobid works' '
	rank1=$(flux exec --jobid=$id1 flux getattr rank) &&
	rank2=$(flux exec --jobid=$id2 flux getattr rank) &&
	test_debug "echo flux exec --jobid=$id1 ran on rank $rank1" &&
	test_debug "echo flux exec --jobid=$id2 ran on rank $rank2" &&
	test $rank1 -eq $(flux jobs -no {ranks} $id1) &&
	test $rank2 -eq $(flux jobs -no {ranks} $id2)
'
test_expect_success 'run one job on two ranks' '
	jobid=$(flux submit --wait-event=start -N2 sleep inf)
'
test_expect_success 'flux exec --jobid on multi-node jobs runs on all ranks' '
	flux exec --jobid=$jobid --label-io flux getattr rank >2node.out &&
	test_debug "cat 2node.out" &&
	grep "2: 2" 2node.out &&
	grep "3: 3" 2node.out
'
test_expect_success 'flux exec --jobid works with --rank option' '
	flux exec --jobid=$jobid -r 0 flux getattr rank &&
	test $(flux exec --jobid=$jobid -r 0 flux getattr rank) -eq 2 &&
	flux exec --jobid=$jobid -r 1 flux getattr rank &&
	test $(flux exec --jobid=$jobid -r 1 flux getattr rank) -eq 3
'
test_expect_success 'flux exec --jobid fails with invalid --rank option' '
	test_must_fail flux exec --jobid=$jobid -r 3 hostname &&
	test_must_fail flux exec --jobid=$jobid -r 0-3 hostname
'
test_expect_success 'flux exec --jobid works with --exclude option' '
	flux exec --jobid=$jobid -x 0 flux getattr rank &&
	test $(flux exec --jobid=$jobid -x 0 flux getattr rank) -eq 3 &&
	flux exec --jobid=$jobid -x 1 flux getattr rank &&
	test $(flux exec --jobid=$jobid -x 1 flux getattr rank) -eq 2
'
test_expect_success 'flux exec --jobid ignores invalid --exclude ranks' '
	test_debug "flux exec --jobid=$jobid -l -x 3-4 flux getattr rank" &&
	test $(flux exec --jobid=$jobid -x 1-5 flux getattr rank) -eq 2
'
test_expect_success 'flux exec --jobid fails if there are no ranks to target' '
	test_must_fail flux exec --jobid=$jobid -x 0-1 hostname
'
test_exec() {
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=$1 \
	    flux exec -r$3 --jobid=$2 id
}
test_expect_success FLUX_SECURITY 'flux exec --jobid fails from other user' '
	alt_userid=$(($(id -u)+1)) &&
	test_debug "echo testing with handle userid=$alt_userid" &&
	test_must_fail test_exec $alt_userid $jobid 0 2>eperm0.err &&
	test_debug "cat eperm0.err" &&
	grep "failed to get shell.init event" eperm0.err &&
	test_must_fail test_exec $alt_userid $jobid 1 2>eperm1.err &&
	test_debug "cat eperm1.err" &&
	grep "failed to get shell.init event" eperm1.err
'
job_service() {
    flux job wait-event --format=json -p exec $1 shell.init \
        | jq -r .context.service
}
# Usage: test_exec_direct userid rolemask jobid
test_exec_direct() {
	service=$(job_service $2).rexec && \
	ranks=$(flux jobs -n --format="{ranks}" $2) && \
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=$1 \
	    flux exec -r$ranks --service=$service id
}
test_expect_success FLUX_SECURITY 'flux exec direct to shell fails also' '
	alt_userid=$(($(id -u)+1)) &&
	test_must_fail test_exec_direct $alt_userid $jobid 2>eperm.err &&
	test_debug "cat eperm.err" &&
	grep "requires owner credentials" eperm.err
'
test_expect_success 'cancel 2 node job' '
	flux cancel $jobid
'
# The following tests cover RFC 42 request signing using the rexec.sign-required
# shell option, which forces the shell subprocess server to require signed
# requests even in a single-user instance.  This allows coverage of the
# signing code paths without requiring a real multi-user setup.
#
# A server without a security context (sec == NULL) rejects any request
# carrying a "signature" field with EPERM ("signature verification not
# available"), so --sign against a normal job is expected to fail.
test_expect_success SIGN_WRAP 'flux exec --sign fails on !sign-required job' '
	test_must_fail flux exec --jobid=$id1 --sign flux getattr rank
'
test_expect_success SIGN_WRAP 'run job requiring signed exec requests' '
	sign_jobid=$(flux submit --wait-event=start \
		-o rexec.sign-required=1 sleep inf) &&
	sign_service=$(job_service $sign_jobid).rexec &&
	sign_rank=$(flux jobs -no {ranks} $sign_jobid)
'
test_expect_success SIGN_WRAP 'unsigned exec to sign-required job fails' '
	test_must_fail flux exec --jobid=$sign_jobid hostname 2>unsigned.err &&
	test_debug "cat unsigned.err" &&
	grep "request signature required" unsigned.err
'
test_expect_success SIGN_WRAP 'signed exec to sign-required job works' '
	flux exec --jobid=$sign_jobid --sign true
'
test_expect_success SIGN_WRAP 'signed exec with wrong credential userid fails' '
	alt_userid=$(($(id -u)+1)) &&
	test_must_fail \
	    env FLUX_HANDLE_ROLEMASK=0x1 FLUX_HANDLE_USERID=$alt_userid \
	    flux exec --sign --jobid=$sign_jobid hostname \
	    2>credmismatch.err &&
	test_debug "cat credmismatch.err" &&
	grep "signing userid does not match requestor" credmismatch.err
'
test_expect_success SIGN_WRAP 'signed exec uses signed write requests' '
	flux lptest > test.txt &&
	flux exec --jobid=$sign_jobid --sign cat <test.txt >output.txt &&
	test_cmp test.txt output.txt
'
# N.B. The 'ps' command below hangs under ASAN.  Temporarily unset LD_PRELOAD
# so 'ps' works in the check below.
SAVE_LD_PRELOAD=${LD_PRELOAD}
if test_have_prereq ASAN; then
    LD_PRELOAD=""
fi

# The version of stdbuf(1) in older versions of uutils/coreutils does
# not exec() its argument but instead remains the parent and collects
# exit status. This version does not forward signals to children, so
# it breaks the test below. Detect versions of stdbuf that don't exec
# their arguments and skip the test if found.
if test $(stdbuf --output=L sh -c 'ps -q $PPID -o comm=') != "stdbuf"; then
	test_set_prereq WORKING_STDBUF
fi

LD_PRELOAD=${SAVE_LD_PRELOAD}

waitfile=$SHARNESS_TEST_SRCDIR/scripts/waitfile.lua
test_expect_success WORKING_STDBUF,SIGN_WRAP \
	'signal forwarding works with signed requests' '
	cat >test_signal.sh <<-EOF &&
	#!/bin/bash
	sig=\${1-INT}
	rm -f sleepready.out
	mkfifo input.fifo
	stdbuf --output=L \
	    flux exec --sign --jobid=$sign_jobid -v -n \
	        awk "BEGIN {print \"hi\"} {print}" input.fifo \
	        >sleepready.out &
	$waitfile -vt 20 -p ^hi sleepready.out &&
	kill -\$sig %1 &&
	wait %1
	exit \$?
	EOF
	chmod +x test_signal.sh &&
	test_expect_code 130 run_timeout 20 ./test_signal.sh INT &&
	test_expect_code 143 run_timeout 20 ./test_signal.sh TERM
'
test_expect_success SIGN_WRAP 'unsigned kill to sign-required server is rejected' '
	flux exec --waitable --bg --label=test --sign --jobid=$sign_jobid sleep inf &&
	test_must_fail \
		flux sproc kill --service=$sign_service --rank=$sign_rank 15 test \
		2>unsigned-kill.err &&
	test_debug "cat unsigned-kill.err" &&
	grep "signature required" unsigned-kill.err
'
test_expect_success SIGN_WRAP 'unsigned list to sign-required server is rejected' '
	test_must_fail \
		flux sproc ps --service=$sign_service --rank=$sign_rank \
		2>unsigned-ps.err &&
	test_debug "cat unsigned-ps.err" &&
	grep "signature required" unsigned-ps.err
'
test_expect_success SIGN_WRAP 'unsigned wait to sign-required server is rejected' '
	test_must_fail \
		flux sproc wait --service=$sign_service --rank=$sign_rank test \
		2>unsigned-wait.err &&
	test_debug "cat unsigned-wait.err" &&
	grep "signature required" unsigned-wait.err
'
test_expect_success SIGN_WRAP 'create kill-signed.py' '
	cat <<-EOF >kill-signed.py &&
	import os, signal, sys
	import flux, flux.subprocess as sp
	h = flux.Flux()
	svc = os.environ.get("SIGN_SERVICE")
	rank = int(os.environ.get("SIGN_RANK"))
	label = sys.argv[1]
	print(f"sp.kill(h, label={label}, service={svc}, nodeid={rank}, sign=True)")
	sp.kill(h, label=label, service=svc, nodeid=rank, sign=True).get()
	EOF
	chmod +x kill-signed.py
'
test_expect_success SIGN_WRAP 'kill background process' '
	SIGN_SERVICE=$sign_service SIGN_RANK=$sign_rank \
	    flux python kill-signed.py test
'
test_expect_success SIGN_WRAP \
	'unsigned exec to non-sign-required server still works after sign setup' '
	flux exec --jobid=$id1 true
'
test_expect_success 'cancel jobs' '
	flux cancel --all &&
	flux job wait-event $id1 clean
'
test_expect_success 'flux exec --jobid on inactive job fails' '
	test_must_fail flux exec --jobid=$id1 hostname 2>inactive.err &&
	test_debug "cat inactive.err" &&
	grep "not currently running" inactive.err
'

test_done
