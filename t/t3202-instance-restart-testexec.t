#!/bin/sh

test_description='Test instance restart and still running jobs with testexec'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'run a testexec job in persistent instance (long run)' '
	flux start -o,--setattr=statedir=$(pwd) \
	     -o,--setattr=broker.shutdown_path= \
	     flux submit \
	       --flags=debug \
	       --setattr=system.exec.test.run_duration=100s \
	       hostname >id1.out
'

test_expect_success 'restart instance, reattach to running job, cancel it (long run)' '
	flux start -o,--setattr=statedir=$(pwd) \
	     -o,--setattr=broker.shutdown_path= \
	     sh -c "flux job eventlog $(cat id1.out) > eventlog_long1.out; \
		    flux jobs -n > jobs_long1.out; \
		    flux cancel $(cat id1.out)" &&
	grep "reattach-start" eventlog_long1.out &&
	grep "reattach-finish" eventlog_long1.out &&
	grep $(cat id1.out) jobs_long1.out
'

test_expect_success 'restart instance, job completed (long run)' '
	flux start -o,--setattr=statedir=$(pwd) \
	     -o,--setattr=broker.shutdown_path= \
	     sh -c "flux job eventlog $(cat id1.out) > eventlog_long2.out; \
		    flux jobs -n > jobs_long2.out" &&
	grep "finish" eventlog_long2.out | grep status &&
	test_must_fail grep $(cat id1.out) jobs_long2.out
'

# reattach_finish will indicate to testexec that the job finished
# right after reattach, emulating a job that finished before the
# instance restarted
test_expect_success 'run a testexec job in persistent instance (exit run)' '
	flux start -o,--setattr=statedir=$(pwd) \
	     -o,--setattr=broker.shutdown_path= \
	     flux submit \
	       --flags=debug \
	       --setattr=system.exec.test.reattach_finish=1 \
	       --setattr=system.exec.test.run_duration=100s \
	     hostname >id2.out
'

test_expect_success 'restart instance, reattach to running job, its finished (exit run)' '
	flux start -o,--setattr=statedir=$(pwd) \
	     -o,--setattr=broker.shutdown_path= \
	     sh -c "flux job eventlog $(cat id2.out) > eventlog_exit1.out" &&
	grep "reattach-start" eventlog_exit1.out &&
	grep "reattach-finish" eventlog_exit1.out &&
	grep "finish" eventlog_exit1.out | grep status
'

test_done
