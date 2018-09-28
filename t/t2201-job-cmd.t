#!/bin/sh

test_description='Test flux job command'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

if flux job submitbench --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
fi

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec

# 2^64 - 1
MAXJOBID_DEC=18446744073709551615
MAXJOBID_KVS="job.active.ffff.ffff.ffff.ffff"
MAXJOBID_WORDS="natural-analyze-verbal--natural-analyze-verbal"

MINJOBID_DEC=0
MINJOBID_KVS="job.active.0000.0000.0000.0000"
MINJOBID_WORDS="academy-academy-academy--academy-academy-academy"

test_under_flux 1 job

flux setattr log-stderr-level 1

test_expect_success 'flux-job: unknown sub-command fails with usage message' '
	test_must_fail flux job wrongsubcmd 2>usage.out &&
	grep -q Usage: usage.out
'

test_expect_success 'flux-job: missing sub-command fails with usage message' '
	test_must_fail flux job 2>usage2.out &&
	grep -q Usage: usage2.out
'

test_expect_success 'flux-job: submitbench with no jobspec fails with usage' '
	test_must_fail flux job submitbench 2>usage3.out &&
	grep -q Usage: usage3.out
'

test_expect_success 'flux-job: submitbench with nonexistent jobpsec fails' '
	test_must_fail flux job submitbench /noexist
'

test_expect_success 'flux-job: submitbench with bad broker connection fails' '
	FLUX_URI=/wrong \
	test_must_fail flux job submitbench \
	    --sign-type=none \
	    ${JOBSPEC}/valid/basic.yaml
'

test_expect_success HAVE_FLUX_SECURITY 'flux-job: submitbench with bad security config fails' '
	test_must_fail flux job submitbench \
	    --sign-type=none \
            --security-config=/nonexist \
	    ${JOBSPEC}/valid/basic.yaml
'

test_expect_success HAVE_FLUX_SECURITY 'flux-job: submitbench with bad sign type fails' '
	test_must_fail flux job submitbench \
	    --sign-type=notvalid \
	    ${JOBSPEC}/valid/basic.yaml
'

test_expect_success 'flux-job: id without from/to args is dec to dec' '
	jobid=$(flux job id 42) &&
	test "$jobid" = "42"
'

test_expect_success 'flux-job: id from stdin works' '
	jobid=$(echo 42 | flux job id) &&
	test "$jobid" = "42"
'

test_expect_success 'flux-job: id with invalid from/to arg fails' '
	test_must_fail flux job id --from=invalid 42 &&
	test_must_fail flux job id --to=invalid 42
'

test_expect_success 'flux-job: id --from=dec works' '
	jobid=$(flux job id --from=dec $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_DEC" &&
	jobid=$(flux job id --from=dec $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_DEC"
'

test_expect_success 'flux-job: id --from=words works' '
	jobid=$(flux job id --from=words $MAXJOBID_WORDS) &&
	test "$jobid" = "$MAXJOBID_DEC" &&
	jobid=$(flux job id --from=words $MINJOBID_WORDS) &&
	test "$jobid" = "$MINJOBID_DEC"
'

test_expect_success 'flux-job: id --from=kvs-active works' '
	jobid=$(flux job id --from=kvs-active $MAXJOBID_KVS) &&
	test "$jobid" = "$MAXJOBID_DEC" &&
	jobid=$(flux job id --from=kvs-active $MINJOBID_KVS) &&
	test "$jobid" = "$MINJOBID_DEC"
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

test_expect_success 'flux-job: id --to=kvs-active works' '
	jobid=$(flux job id --to=kvs-active $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_KVS" &&
	jobid=$(flux job id --to=kvs-active $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_KVS"
'

test_expect_success 'flux-job: id --from=kvs-active fails on bad input' '
	test_must_fail flux job id --from=kvs-active badstring &&
	test_must_fail flux job id --from=kvs-active \
	    job.active.0000.0000 &&
	test_must_fail flux job id --from=kvs-active \
	    job.active.0000.0000.0000.000P
'

test_expect_success 'flux-job: id --from=dec fails on bad input' '
	test_must_fail flux job id --from=dec 42plusbad &&
	test_must_fail flux job id --from=dec meep &&
	test_must_fail flux job id --from=dec 18446744073709551616
'

test_expect_success 'flux-job: id --from=words fails on bad input' '
	test_must_fail flux job id --from=words badwords
'

test_expect_success 'flux-job: id works with spaces in input' '
	(echo "42"; echo "42") >despace.exp &&
	(echo "42 "; echo " 42") | flux job id >despace.out &&
	test_cmp despace.exp despace.out
'

test_done
