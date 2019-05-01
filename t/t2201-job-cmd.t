#!/bin/sh 
test_description='Test flux job command'

. $(dirname $0)/sharness.sh

if flux job submit --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
fi

# 2^64 - 1
MAXJOBID_DEC=18446744073709551615
MAXJOBID_KVS="job.active.ffff.ffff.ffff.ffff"
MAXJOBID_WORDS="natural-analyze-verbal--natural-analyze-verbal"

MINJOBID_DEC=0
MINJOBID_KVS="job.active.0000.0000.0000.0000"
MINJOBID_WORDS="academy-academy-academy--academy-academy-academy"

test_under_flux 1 job

flux setattr log-stderr-level 1

test_expect_success 'flux-job: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
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

test_expect_success 'flux-job: submit with nonexistent jobpsec fails' '
	test_must_fail flux job submit /noexist
'

test_expect_success 'flux-job: submit with bad broker connection fails' '
	! FLUX_URI=/wrong flux job submit basic.json
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

test_expect_success 'flux-job: can submit jobspec on stdin with -' '
        flux job submit - <basic.json
'

test_expect_success 'flux-job: can submit jobspec on stdin without -' '
        flux job submit <basic.json
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

test_expect_success 'flux-job: id --from=kvs works' '
	jobid=$(flux job id --from=kvs $MAXJOBID_KVS) &&
	test "$jobid" = "$MAXJOBID_DEC" &&
	jobid=$(flux job id --from=kvs $MINJOBID_KVS) &&
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

test_expect_success 'flux-job: id --to=kvs works' '
	jobid=$(flux job id --to=kvs $MAXJOBID_DEC) &&
	test "$jobid" = "$MAXJOBID_KVS" &&
	jobid=$(flux job id --to=kvs $MINJOBID_DEC) &&
	test "$jobid" = "$MINJOBID_KVS"
'

test_expect_success 'flux-job: id --from=kvs fails on bad input' '
	test_must_fail flux job id --from=kvs badstring &&
	test_must_fail flux job id --from=kvs \
	    job.active.0000.0000 &&
	test_must_fail flux job id --from=kvs \
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

test_expect_success 'flux-job: priority fails with bad FLUX_URI' '
	! FLUX_URI=/wrong flux job priority ${validjob} 0
'

test_expect_success 'flux-job: priority fails with non-numeric jobid' '
	test_must_fail flux job priority foo 0
'

test_expect_success 'flux-job: priority fails with wrong number of arguments' '
	test_must_fail flux job priority ${validjob}
'

test_expect_success 'flux-job: priority fails with non-numeric priority' '
	test_must_fail flux job priority ${validjob} foo
'

test_expect_success 'flux-job: raise fails with bad FLUX_URI' '
	! FLUX_URI=/wrong flux job raise ${validjob}
'

test_expect_success 'flux-job: raise fails with no args' '
	test_must_fail flux job raise
'

test_expect_success 'flux-job: raise fails with invalid jobid' '
	test_must_fail flux job raise foo
'

test_expect_success 'flux-job: raise fails with invalid option' '
	test_must_fail flux job raise --meep foo
'

test_expect_success 'flux-job: cancel fails with bad FLUX_URI' '
	! FLUX_URI=/wrong flux job cancel ${validjob}
'

test_expect_success 'flux-job: cancel fails with no args' '
	test_must_fail flux job cancel
'

test_expect_success 'flux-job: cancel fails with invalid jobid' '
	test_must_fail flux job cancel foo
'

test_expect_success 'flux-job: cancel fails with invalid option' '
	test_must_fail flux job cancel --meep foo
'

test_expect_success 'flux-job: list fails with bad FLUX_URI' '
	! FLUX_URI=/wrong flux job list
'

test_expect_success 'flux-job: list fails with wrong number of arguments' '
	test_must_fail flux job list foo
'

test_expect_success 'flux-job: list -s suppresses header' '
	flux job list >list.out &&
	grep -q JOBID list.out &&
	flux job list -s >list_s.out &&
	test_must_fail grep -q JOBID list_s.out
'

test_expect_success 'flux-job: id works with spaces in input' '
	(echo "42"; echo "42") >despace.exp &&
	(echo "42 "; echo " 42") | flux job id >despace.out &&
	test_cmp despace.exp despace.out
'

test_done
