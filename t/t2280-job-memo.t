#!/bin/sh
test_description='Test flux job memo command'

. $(dirname $0)/job-manager/sched-helper.sh
. $(dirname $0)/sharness.sh

if flux job submit --help 2>&1 | grep -q sign-type; then
	test_set_prereq HAVE_FLUX_SECURITY
fi

test_under_flux 1 job -Slog-stderr-level=1

runas() {
        userid=$1 && shift
        FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

test_expect_success 'memo: error on insufficient arguments' '
	test_expect_code 1 flux job memo 1234 &&
	test_expect_code 1 flux job memo
'
test_expect_success 'memo: error on invalid jobid' '
	test_expect_code 1 flux job memo f1 foo=bar
'
test_expect_success 'memo: create one inactive job' '
	flux submit true >inactivejob &&
	flux queue drain
'
test_expect_success 'memo: submit a running and pending job' '
	flux bulksubmit --urgency={} sleep 300 ::: 16 16 0 >jobids &&
	runid=$(head -n 1 jobids) &&
	pendingid=$(tail -n 1 jobids) &&
	flux job wait-event $runid start
'
test_expect_success 'memo: only job owner can add memo' '
	test_expect_code 1 \
	    runas 9999 flux job memo $pendingid foo=24 2>memo.guest.err &&
	test_debug "cat memo.guest.err" &&
	grep "guests can only add a memo to their own jobs" memo.guest.err
'
test_expect_success 'memo: memo cannot be added to inactive job' '
	test_must_fail flux job memo $(cat inactivejob) foo=42
'
test_expect_success 'memo: add memo to pending job works' '
	flux job memo $pendingid foo=42 a=b &&
	flux job wait-event -t 10 $pendingid memo &&
	jmgr_check_memo $pendingid foo 42
'
test_expect_success 'memo: remove memo from pending job works' '
	flux job memo $pendingid foo=null &&
	flux job wait-event -m foo=null -t 10 $pendingid memo &&
	test_expect_code 1 jmgr_check_memo_exists $pendingid foo
'
test_expect_success 'memo: add volatile memo to pending job works' '
	flux job memo --volatile $pendingid foo=bar &&
	jmgr_check_memo $pendingid foo \"bar\" &&
	flux job eventlog $pendingid > eventlog.pending &&
	test_expect_code 1 grep foo=bar eventlog.pending
'
test_expect_success 'memo: add memo to running job works' '
	flux job memo $runid foo=42 &&
	flux job wait-event -t 10 $runid memo &&
	jmgr_check_memo $runid foo 42
'
test_expect_success 'memo: remove memo from running job works' '
	flux job memo $runid foo=null &&
	flux job wait-event -m foo=null -t 10 $runid memo &&
	test_expect_code 1 jmgr_check_memo_exists $runid foo
'
test_expect_success 'memo: flux job memo works with dotted path' '
	flux job memo $runid a.b.c=42 a.d=test &&
	jmgr_check_memo_exists $runid a.b.c &&
	jmgr_check_memo_exists $runid a.d
'
test_expect_success 'memo: flux job memo works with key=- (stdin)' '
	echo "xyz" | flux job memo $runid a.f=-A &&
	jmgr_check_memo_exists $runid a.f
'
test_expect_success 'memo: flux job memo: null values unset keys' '
	flux job memo $runid a.b=null &&
	flux job wait-event -v -m "a={\"b\":null}" -t 10 $runid memo &&
	test_expect_code 1 jmgr_check_memo_exists $runid a.b &&
	jmgr_check_memo $runid a.d \"test\"
'
test_expect_success 'memo: available in flux-jobs {user} attribute' '
	jlist_check_memo $runid a.d \"test\" &&
	jlist_check_memo $pendingid a \"b\"
'
test_expect_success 'memo: reload job-list module' '
	flux module reload job-list
'
test_expect_success 'memo: non-volatile memos still available in job-list' '
	jlist_check_memo $runid a.d \"test\" &&
	jlist_check_memo $pendingid a \"b\"
'
test_expect_success 'memo: cancel all jobs' '
	flux cancel --all &&
	flux job wait-event $runid clean &&
	flux job wait-event $pendingid clean
'
test_expect_success 'memo: non-volatile memos still available for jobs' '
	jlist_check_memo $runid a.d \"test\" &&
	jlist_check_memo $pendingid a \"b\"
'
test_done
