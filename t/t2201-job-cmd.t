#!/bin/sh
test_description='Test flux job command'

. $(dirname $0)/sharness.sh

if flux job submit --help 2>&1 | grep -q sign-type; then
	test_set_prereq HAVE_FLUX_SECURITY
fi

# 2^64 - 1
MAXJOBID_DEC=18446744073709551615
MAXJOBID_HEX="0xffffffffffffffff"
MAXJOBID_KVS="job.ffff.ffff.ffff.ffff"
MAXJOBID_DOTHEX="ffff.ffff.ffff.ffff"
MAXJOBID_WORDS="natural-analyze-verbal--natural-analyze-verbal"
MAXJOBID_F58="fjpXCZedGfVQ"
MAXJOBIDS_LIST="$MAXJOBID_DEC $MAXJOBID_HEX $MAXJOBID_KVS $MAXJOBID_DOTHEX $MAXJOBID_WORDS $MAXJOBID_F58"

MINJOBID_DEC=0
MINJOBID_HEX="0x0"
MINJOBID_KVS="job.0000.0000.0000.0000"
MINJOBID_DOTHEX="0000.0000.0000.0000"
MINJOBID_WORDS="academy-academy-academy--academy-academy-academy"
MINJOBID_F58="f1"
MINJOBIDS_LIST="$MINJOBID_DEC $MINJOBID_HEX $MINJOBID_KVS $MINJOBID_DOTHEX $MINJOBID_WORDS $MINJOBID_F58"

test_under_flux 2 job -Slog-stderr-level=1

# Other tests may refer to $(cat inactivejob) for inactive job id
test_expect_success 'create one inactive job' '
	flux submit true >inactivejob &&
	flux queue drain
'

# After this, new jobs will remain in SCHED state
test_expect_success 'unload job-exec,sched-simple modules' '
	flux module remove job-exec &&
	flux module remove sched-simple
'

test_expect_success 'flux-job: generate jobspec for simple test job' '
	flux run --dry-run -n1 hostname >basic.json
'

test_expect_success 'flux-job: submit one job to get one valid job in queue' '
	validjob=$(flux job submit basic.json) &&
	echo Valid job is ${validjob}
'

test_expect_success 'flux-job: submit --flags=badflag fails with unknown flag' '
	! flux job submit --flags=badflag basic.json 2>badflag.out &&
	grep -q "unknown flag" badflag.out
'

test_expect_success 'flux-job: unknown sub-command fails with usage message' '
	test_must_fail flux job wrongsubcmd 2>usage.out &&
	grep -q Usage: usage.out
'

test_expect_success 'flux-job: missing sub-command fails with usage message' '
	test_must_fail flux job 2>usage2.out &&
	grep -q Usage: usage2.out
'

test_expect_success 'flux-job: submit with empty jobpsec fails' '
	test_must_fail flux job submit </dev/null
'

test_expect_success 'flux-job: submit with nonexistent jobpsec fails' '
	test_must_fail flux job submit /noexist
'

test_expect_success 'flux-job: submit with bad broker connection fails' '
	(FLUX_URI=/wrong test_must_fail flux job submit basic.json)
'

test_expect_success HAVE_FLUX_SECURITY 'flux-job: submit with bad security config fails' '
	test_must_fail flux job submit \
		--security-config=/nonexist \
		basic.json
'

test_expect_success HAVE_FLUX_SECURITY 'flux-job: submit with bad sign type fails' '
	test_must_fail flux job submit \
		--sign-type=notvalid \
		basic.json
'
test_expect_success HAVE_FLUX_SECURITY 'flux-job: submit ignores security-config with --flags=signed' '
	flux run --dry-run -n1 hostnane | \
	    flux python \
	    ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $(id -u) >signed.json &&
	flux job submit --security-config=/nonexist --flags=signed \
		signed.json >submit-signed.out 2>&1 &&
	grep "Ignoring security config" submit-signed.out
'
test_expect_success HAVE_FLUX_SECURITY 'flux-job: submit as root fails' '
	flux run --dry-run -n1 hostname | \
	    flux python \
	    ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py 0 >signed0.json &&
	    ( export FLUX_HANDLE_USERID=0 &&
	      test_must_fail \
	          flux job submit --flags=signed signed0.json 2>signed0.err \
	    ) &&
	test_debug "cat signed0.err" &&
        grep "submission of jobs as user root not supported" signed0.err
'
test_expect_success 'flux-job: can submit jobspec on stdin with -' '
	flux job submit - <basic.json
'

test_expect_success 'flux-job: can submit jobspec on stdin without -' '
	flux job submit <basic.json
'

test_expect_success 'flux-job: id without to arg is dec to dec' '
	jobid=$(flux job id 42) &&
	test "$jobid" = "42"
'

test_expect_success 'flux-job: id from stdin works' '
	jobid=$(echo 42 | flux job id) &&
	test "$jobid" = "42"
'

test_expect_success 'flux-job: id with invalid --to arg fails' '
	test_must_fail flux job id --to=invalid 42
'

test_expect_success 'flux-job: fails with no input and no args' '
	test_expect_code 1 flux job id < /dev/null
'

test_expect_success 'flux-job: id works with min/max jobids' '
	for max in $MAXJOBIDS_LIST; do
		jobid=$(flux job id $max) &&
		test_debug "echo flux jobid $max -> $jobid" &&
		test "$jobid" = "$MAXJOBID_DEC"
	done &&
	for min in $MINJOBIDS_LIST; do
		jobid=$(flux job id $min) &&
		test_debug "echo flux jobid $min -> $jobid" &&
		test "$jobid" = "$MINJOBID_DEC"
	done
'

test_expect_success 'flux-job: id --to=dec works' '
	jobid=$(flux job id --to=dec $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_DEC" &&
	jobid=$(flux job id --to=dec $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_DEC"
'

test_expect_success 'flux-job: id --to=words works' '
	jobid=$(flux job id --to=words $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_WORDS" &&
	jobid=$(flux job id --to=words $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_WORDS"
'

test_expect_success 'flux-job: id --to=kvs works' '
	jobid=$(flux job id --to=kvs $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_KVS" &&
	jobid=$(flux job id --to=kvs $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_KVS"
'

test_expect_success 'flux-job: id --to=hex works' '
	jobid=$(flux job id --to=hex $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_HEX" &&
	jobid=$(flux job id --to=hex $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_HEX"
'

test_expect_success 'flux-job: id --to=dothex works' '
	jobid=$(flux job id --to=dothex $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_DOTHEX" &&
	jobid=$(flux job id --to=dothex $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_DOTHEX"
'

test_expect_success 'flux-job: id --to=f58 works' '
	jobid=$(flux job id --to=f58 $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_F58" &&
	jobid=$(flux job id --to=f58 $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_F58"
'

UTF8_LOCALE=$(locale -a | grep 'UTF-8\|utf8' | head -n1)
if flux version | grep +ascii-only; then
	UTF8_LOCALE=""
fi
test -n "$UTF8_LOCALE" && test_set_prereq UTF8_LOCALE
test_expect_success UTF8_LOCALE 'flux-job: f58 can use multibyte prefix' '
	test_debug "echo UTF8_LOCALE=${UTF8_LOCALE}" &&
	jobid=$(LC_ALL=${UTF8_LOCALE} flux job id --to=f58 1) &&
	test "$jobid" = "ƒ2"
'

test_expect_success 'flux-job: id fails on bad input' '
	test_must_fail flux job id badstring &&
	test_must_fail flux job id job.0000.0000 &&
	test_must_fail flux job id job.0000.0000.0000.000P
'

test_expect_success 'flux-job: id fails on bad input' '
	test_must_fail flux job id 42plusbad &&
	test_must_fail flux job id meep &&
	test_must_fail flux job id 18446744073709551616
'

test_expect_success 'flux-job: id fails on bad words input' '
	test_must_fail flux job id bad-words
'

test_expect_success 'flux-job: urgency fails with bad FLUX_URI' '
	(FLUX_URI=/wrong test_must_fail flux job urgency ${validjob} 0)
'

test_expect_success 'flux-job: urgency fails with non-numeric jobid' '
	test_must_fail flux job urgency foo 0
'

test_expect_success 'flux-job: urgency fails with wrong number of arguments' '
	test_must_fail flux job urgency ${validjob}
'

test_expect_success 'flux-job: urgency fails with non-numeric urgency' '
	test_must_fail flux job urgency ${validjob} foo
'

test_expect_success 'job-manager: flux job urgency fails on invalid jobid' '
	test_must_fail flux job urgency 12345 31
'

test_expect_success 'job-manager: flux job urgency fails on inactive jobid' '
	test_must_fail flux job urgency $(cat inactivejob) 31
'

test_expect_success 'flux-job: raise fails with bad FLUX_URI' '
	(FLUX_URI=/wrong test_must_fail flux job raise ${validjob})
'

test_expect_success 'flux-job: raise fails with no args' '
	test_must_fail flux job raise
'

test_expect_success 'flux-job: raise fails with invalid jobid' '
	test_must_fail flux job raise foo
'

test_expect_success 'flux-job: raise fails with invalid jobids' '
	test_must_fail flux job raise foo fee
'

test_expect_success 'flux-job: raise fails if note set twice' '
	test_must_fail flux job raise --message=hi ${validjob} -- hello
'

test_expect_success 'flux-job: raise fails with inactive jobid' '
	test_must_fail flux job raise $(cat inactivejob)
'

test_expect_success 'flux-job: raise fails with invalid option' '
	test_must_fail flux job raise --meep foo
'

test_expect_success 'flux-job: raise basic works' '
	id=$(flux submit sleep 100) &&
	flux job raise ${id} &&
	flux job wait-event -t 30 ${id} exception >raise1.out &&
	grep "cancel" raise1.out &&
	grep "severity\=0" raise1.out
'

test_expect_success 'flux-job: raise --type works' '
	id=$(flux submit sleep 100) &&
	flux job raise -t typefoo ${id} &&
	flux job wait-event -t 30 ${id} exception >raise2.out &&
	grep "typefoo" raise2.out
'

test_expect_success 'flux-job: raise --severity works' '
	id=$(flux submit sleep 100) &&
	flux job raise --severity=5 ${id} &&
	flux job wait-event -t 30 ${id} exception >raise3.out &&
	grep "severity\=5" raise3.out &&
	flux cancel ${id}
'

test_expect_success 'flux-job: raise --message works' '
	id=$(flux submit sleep 100) &&
	flux job raise --message=foobarmessage ${id} &&
	flux job wait-event -t 30 ${id} exception >raise4.out &&
	grep "foobarmessage" raise4.out
'

test_expect_success ' flux-job: raise message works (cmdline)' '
	id=$(flux submit sleep 100) &&
	flux job raise ${id} -- eep ork ook &&
	flux job wait-event -t 30 ${id} exception >raise5.out &&
	grep "eep ork ook" raise5.out
'

test_expect_success 'flux-job: list fails with bad FLUX_URI' '
	(FLUX_URI=/wrong test_must_fail flux job list)
'

test_expect_success 'flux-job: list fails with wrong number of arguments' '
	test_must_fail flux job list foo
'

test_expect_success 'flux-job: id works with spaces in input' '
	(echo "42"; echo "42") >despace.exp &&
	(echo "42 "; echo " 42") | flux job id >despace.out &&
	test_cmp despace.exp despace.out
'

test_expect_success 'flux-job: namespace works' '
	test "$(flux job namespace 1)" = "job-1" &&
	test "$(echo 1 | flux job namespace)" = "job-1" &&
	test_expect_code 1 flux job namespace -1
'

test_expect_success 'flux-job: attach fails without jobid argument' '
	test_must_fail flux job attach
'

test_expect_success 'flux-job: attach fails on invalid jobid' '
	test_must_fail flux job attach $(($(flux job id ${validjob})+1))
'

test_expect_success 'flux-job: kill fails without jobid argument' '
	test_must_fail flux job kill
'

test_expect_success 'flux-job: kill fails on invalid jobid' '
	test_expect_code 1 flux job kill $(($(flux job id ${validjob})+1))
'

test_expect_success 'flux-job: kill fails on non-running job' '
	test_expect_code 1 flux job kill ${validjob} 2>kill.err &&
	cat <<-EOF >kill.expected &&
	flux-job: kill ${validjob}: job is not running
	EOF
	test_cmp kill.expected kill.err
'
test_expect_success 'flux-job: kill fails on inactive job' '
	test_expect_code 1 flux job kill $(cat inactivejob) 2>kill2.err &&
	grep "job is inactive" kill2.err
'

test_expect_success 'flux-job: kill fails with invalid signal name' '
	test_expect_code 1 flux job kill -s SIGFAKE ${validjob} 2>kill.err2 &&
	cat <<-EOF >kill.expected2 &&
	flux-job: kill: Invalid signal SIGFAKE
	EOF
	test_cmp kill.expected2 kill.err2
'

test_expect_success 'flux-job: kill fails with invalid signal number' '
	test_expect_code 1 flux job kill -s 0 ${validjob} 2>kill.err2 &&
	cat <<-EOF >kill.expected2 &&
	flux-job: kill: Invalid signal 0
	EOF
	test_cmp kill.expected2 kill.err2
'

test_expect_success 'flux-job: kill fails with invalid signal number' '
	test_expect_code 1 flux job kill -s 144 ${validjob} 2>kill.err2 &&
	cat <<-EOF >kill.expected2 &&
	flux-job: kill ${validjob}: Invalid signal number
	EOF
	test_cmp kill.expected2 kill.err2
'
runas() {
	userid=$1 && shift
	FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

test_expect_success 'flux-job: kill fails for wrong userid' '
	test_expect_code 1 \
		runas 9999 flux job kill ${validjob} 2> kill.guest.err &&
	cat <<-EOF >kill.guest.expected &&
	flux-job: kill ${validjob}: guests may only send signals to their own jobs
	EOF
	test_cmp kill.guest.expected kill.guest.err
'

# Verify that the above tests left some jobs in the queue since
# some tests below rely on it
test_expect_success 'flux job: the queue contains active jobs' '
	count=$(flux job list | wc -l) &&
	test ${count} -gt 0
'

# Note: in this script job-exec is not loaded so there cannot
# be any running jobs to kill

test_expect_success 'flux job: killall with no args works' '
	flux job killall 2>killall_0.err &&
	cat <<-EOT >killall_0.exp &&
	flux-job: Command matched 0 jobs
	EOT
	test_cmp killall_0.exp killall_0.err
'

test_expect_success 'flux-job: killall with bad broker connection fails' '
	(FLUX_URI=/wrong test_must_fail flux job killall)
'

test_expect_success 'flux job: killall with extra free args prints usage' '
	test_must_fail flux job killall foo 2>killall_xargs.err &&
	grep Usage killall_xargs.err
'

test_expect_success 'flux job: killall --force works' '
	flux job killall --force 2>killall_0f.err &&
	test_cmp killall_0.exp killall_0f.err
'

test_expect_success 'flux job: killall fails for invalid signal name' '
	test_must_fail flux job killall -s SIGFAKE 2>killall.err &&
	cat <<-EOT >killall.exp &&
	flux-job: killall: Invalid signal SIGFAKE
	EOT
	test_cmp killall.exp killall.err
'

test_expect_success 'flux-job: killall --user all fails for guest' '
	id=$(($(id -u)+1)) &&
	test_must_fail runas ${id} \
		flux job killall --user all 2> killall_all_guest.err &&
	cat <<-EOF >killall_all_guest.exp &&
	flux-job: killall: guests can only kill their own jobs
	EOF
	test_cmp killall_all_guest.exp killall_all_guest.err
'

test_expect_success 'flux-job: killall --user <guest_uid> works for guest' '
	id=$(($(id -u)+1)) &&
	runas ${id} \
		flux job killall --user ${id} 2> killall_guest.err
'

test_expect_success 'flux job: the queue contains active jobs' '
	count=$(flux job list | wc -l) &&
	test ${count} -gt 0
'

test_expect_success 'clear out the queue' '
	flux cancel --all &&
	run_timeout 60 flux queue drain
'

test_expect_success 'flux job: raiseall with no args prints Usage' '
	test_must_fail flux job raiseall 2>raiseall_na.err &&
	grep Usage: raiseall_na.err
'

test_expect_success 'flux job: raiseall with type works' '
	flux job raiseall test 2>raiseall.err &&
	cat <<-EOT >raiseall.exp &&
	flux-job: Command matched 0 jobs
	EOT
	test_cmp raiseall.exp raiseall.err
'

test_expect_success 'flux-job: raiseall with bad broker connection fails' '
	(FLUX_URI=/wrong test_must_fail flux job raiseall test)
'

test_expect_success 'flux job: raiseall with type and reason works' '
	flux job raiseall test I have a raisin 2>raiseall_reason.err &&
	test_cmp raiseall.exp raiseall_reason.err
'

test_expect_success 'flux job: raiseall with --force works' '
	flux job raiseall --force test 2>raiseall_f.err &&
	test_cmp raiseall.exp raiseall_f.err
'

test_expect_success 'flux job: raiseall with unknown state fails' '
	test_must_fail flux job raiseall --states=FOO test 2>raiseall_bs.err &&
	cat <<-EOT >raiseall_bs.exp &&
	flux-job: error parsing --states: FOO is unknown
	EOT
	test_cmp raiseall_bs.exp raiseall_bs.err
'

test_expect_success 'flux job: raiseall with --state=inactive fails' '
	test_must_fail flux job raiseall \
		--states=inactive test 2>raiseall_inactive.err &&
	cat <<-EOT >raiseall_inactive.exp &&
	flux-job: Exceptions cannot be raised on inactive jobs
	EOT
	test_cmp raiseall_inactive.exp raiseall_inactive.err
'

test_expect_success 'flux job: raiseall with empty statemask fails' '
	test_must_fail flux job raiseall --states="" test 2>raiseall_bs2.err &&
	cat <<-EOT >raiseall_bs2.exp &&
	flux-job: no states specified
	EOT
	test_cmp raiseall_bs2.exp raiseall_bs2.err
'

test_expect_success 'flux job: raiseall with invalid severity fails' '
	test_must_fail flux job raiseall --severity=9 test 2>raiseall_sev.err &&
	cat <<-EOT >raiseall_sev.exp &&
	flux-job: raiseall: invalid exception severity
	EOT
	test_cmp raiseall_sev.exp raiseall_sev.err
'

test_expect_success 'flux job: raiseall with invalid type fails' '
	test_must_fail flux job raiseall "bad type" 2>raiseall_bt.err &&
	cat <<-EOT >raiseall_bt.exp &&
	flux-job: raiseall: invalid exception type
	EOT
	test_cmp raiseall_bt.exp raiseall_bt.err
'

test_expect_success 'flux-job: raiseall --user all fails for guest' '
	id=$(($(id -u)+1)) &&
	test_must_fail runas ${id} \
		flux job raiseall --user all test 2> raiseall_all_guest.err &&
	cat <<-EOT >raiseall_all_guest.exp &&
	flux-job: raiseall: guests can only raise exceptions on their own jobs
	EOT
	test_cmp raiseall_all_guest.exp raiseall_all_guest.err
'

test_expect_success 'flux-job: raiseall --user <guest_uid> works for guest' '
	id=$(($(id -u)+1)) &&
	runas ${id} \
		flux job raiseall --user ${id} test 2> raiseall_guest.err
'

test_expect_success 'flux-job: raiseall --user name works' '
	name=$(id -un) &&
	flux job raiseall --user ${name} test 2> raiseall_name.err
'

test_expect_success 'submit 3 test jobs' '
	for i in $(seq 1 3); do flux submit sleep 60 >>jobs; done
'

test_expect_success 'flux-job: raiseall returns correct count' '
	flux job raiseall -s 7 test not_raised 2>raiseall_test.err &&
	cat <<-EOT >raiseall_test.exp &&
	flux-job: Command matched 3 jobs (-f to confirm)
	EOT
	test_cmp raiseall_test.exp raiseall_test.err
'

test_expect_success 'flux-job: raiseall -f returns correct count' '
	flux job raiseall -s 7 -f test raised1 2>raiseall_testf.err &&
	cat <<-EOT >raiseall_testf.exp &&
	flux-job: Raised exception on 3 jobs (0 errors)
	EOT
	test_cmp raiseall_testf.exp raiseall_testf.err
'

test_expect_success 'empty the queue' '
	flux cancel --all &&
	run_timeout 60 flux queue drain
'
test_expect_success 'flux job: retrieve eventlogs' '
	for id in $(cat jobs); do flux job eventlog ${id} >>eventlogs; done
'

test_expect_success 'flux-job: unconfirmed exception was not raised' '
	test_must_fail grep not_raised eventlogs
'

test_expect_success 'flux-job: confirmed non-fatal exception was raised' '
	count=$(grep raised1 eventlogs | wc -l) &&
	test ${count} -eq 3
'

test_expect_success 'flux-job: fatal cancel exception was raised' '
	count=$(grep cancel eventlogs | wc -l) &&
	test ${count} -eq 3
'

test_expect_success 'flux-job: load modules for live kill tests' '
	flux module load sched-simple &&
	flux module load job-exec
'

# N.B. SIGTERM == 15
test_expect_success 'flux-job: kill basic works' '
	id=$(flux submit --wait-event=start sleep 100) &&
	flux job wait-event -vt 30 -p exec $id shell.start &&
	flux job kill ${id} &&
	flux job wait-event -t 30 ${id} finish > kill1.out &&
	grep status=$((15+128<<8)) kill1.out
'

# N.B. SIGUSR1 == 10
test_expect_success 'flux-job: kill --signal works' '
	id=$(flux submit --wait-event=start sleep 100) &&
	flux job wait-event -vt 30 -p exec $id shell.start &&
	flux job kill --signal=SIGUSR1 ${id} &&
	flux job wait-event -t 30 ${id} finish > kill2.out &&
	grep status=$((10+128<<8)) kill2.out
'

test_expect_success 'flux job: killall -f kills one job' '
	id=$(flux submit sleep 600) &&
	flux job wait-event -vt 30 -p exec $id shell.init &&
	flux job killall -f &&
	run_timeout 60 flux queue drain
'

test_expect_success 'flux job: raise can operate on multiple jobs' '
	ids=$(flux submit --bcc=1-3 sleep 600) &&
	flux job raise ${ids} raise multiple jobs &&
	for id in ${ids}; do
		flux job wait-event -t 30 ${id} exception >exception2.out &&
		grep multiple exception2.out
	done
'

# N.B. SIGTERM == 15
test_expect_success 'flux job: kill can operate on multiple jobs' '
	ids=$(flux submit --wait-event=start --bcc=1-3 sleep 600) &&
	for id in ${ids}; do
		flux job wait-event -t 30 -p exec ${id} shell.init
	done &&
	flux job kill ${ids} &&
	for id in ${ids}; do
		flux job wait-event -t 30 ${id} finish >killmulti.out &&
		grep status=$((15+128<<8)) killmulti.out
	done
'
test_expect_success 'flux job: timeleft reports error outside of a job' '
	test_expect_code 1 flux job timeleft
'

test_expect_success 'flux job: timeleft reports large int with no time limit' '
	flux run flux job timeleft > timeleft1 &&
	test $(cat timeleft1) -gt 9999999
'
test_expect_success 'flux job: timeleft -H reports infinity with no time limit' '
	flux run flux job timeleft -H > timeleft1H &&
	grep infinity timeleft1H
'
test_expect_success 'flux job: timeleft works with time limit' '
	flux run -t 1m flux job timeleft >timeleft2 &&
	test_debug "cat timeleft2" &&
	test $(cat timeleft2) -lt 60
'
test_expect_success 'flux job: timeleft -H works with time limit' '
	flux run -t 1m flux job timeleft -H >timeleft2H &&
	grep "[0-9]s$" timeleft2H
'
test_expect_success 'flux job: timeleft works under alloc (and job)' '
	cat <<-EOF >test.sh &&
	flux job timeleft > timeleft3
	flux run flux job timeleft > timeleft4
	EOF
	chmod +x test.sh &&
	flux alloc -n1 -t 5m ./test.sh &&
	test_debug "cat timeleft3" &&
	test $(cat timeleft3) -lt 300 &&
	test_debug "cat timeleft4" &&
	test $(cat timeleft4) -lt 300
'
test_expect_success 'flux job: timeleft works for a jobid' '
	id=$(flux submit --wait-event=start -t 1m sleep 60) &&
	flux job timeleft $id > timeleft5 &&
	test_debug "cat timeleft5" &&
	test $(cat timeleft5) -lt 60
'
test_expect_success 'flux job: timeleft reports 0s for expired job' '
	id=$(flux submit --wait -t0.01s hostname || true) &&
	flux job timeleft $id > timeleft6 &&
	test_debug "cat timeleft6" &&
	test $(cat timeleft6) -eq 0
'
test_expect_success 'flux job: timeleft returns 0 for completed job' '
	id=$(flux submit --wait -t 5d true) &&
	flux job timeleft $id > timeleft7 &&
	test_debug "cat timeleft7" &&
	test $(cat timeleft7) -eq 0
'
test_expect_success 'flux job: timeleft fails for pending job' '
	flux queue stop &&
	id=$(flux submit -t 10m true) &&
	test_expect_code 1 flux job timeleft $id > timeleft8 2>&1 &&
	flux queue start &&
	grep "has not started" timeleft8
'
test_expect_success 'flux job: timeleft fails for invalid jobids' '
	test_expect_code 1 flux job timeleft f1234 &&
	test_expect_code 1 flux job timeleft x1234
'
test_expect_success 'flux job: hostpids fails for invalid jobid' '
	test_expect_code 1 flux job hostpids oof
'
test_expect_success 'flux job: hostpids fails for unknown jobid' '
	test_expect_code 1 flux job hostpids f1234
'
test_expect_success 'flux job: hostpids fails for inactive job' '
	inactive_id=$(flux jobs -f inactive -nc 1 -o {id}) &&
	test_expect_code 1 flux job hostpids $inactive_id
'
# note: sleep inf job shared by next few tests
test_expect_success 'flux job: hostpids works for running job' '
	id=$(flux submit -N2 -n2 --wait-event=start sleep inf) &&
	flux job hostpids $id >hostpids1.out &&
	test_debug "cat hostpids1.out" &&
	test $(grep -c , hostpids1.out) -eq 1
'
test_expect_success 'flux job: hostpids --delimiter works' '
	flux job hostpids --delimiter="\n" $id >hostpids2.out &&
	test_debug "cat hostpids2.out" &&
	test $(wc -l < hostpids2.out) -eq 2
'
test_expect_success 'flux job: hostpids --ranks works' '
	test "$(flux job hostpids --ranks=1 $id)" = "$(tail -n1 hostpids2.out)"
'
test_expect_success 'flux job: hostpids invalid --ranks fails' '
	test_must_fail flux job hostpids --ranks=foo $id
'
test_expect_success 'flux job: hostpids fails for non job owner' '
	uid=$(($(id -u)+1)) &&
	test_must_fail runas ${uid} \
		flux job hostpids $id
'
test_expect_success 'flux job: hostpids -t, --timeout works' '
	id2=$(flux submit --urgency=hold true) &&
	test_must_fail flux job hostpids -t 0.1 $id2
'
test_expect_success 'terminate running jobs' '
	flux cancel --all &&
	flux queue idle
'
test_done
