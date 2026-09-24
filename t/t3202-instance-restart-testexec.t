#!/bin/sh
#
# ci=asan

test_description='Test instance restart and still running jobs with testexec'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

# Disable the rc1 cleanup script that cancels running jobs on shutdown, so
# that jobs remain in RUN state across a restart and reattach is exercised.
export FLUX_DISABLE_JOB_CLEANUP=t

test_expect_success 'run a testexec job in persistent instance (long run)' '
	flux start --setattr=statedir=$(pwd) \
	     flux submit \
	       --flags=debug \
	       --setattr=system.exec.test.run_duration=100s \
	       hostname >id1.out
'

test_expect_success 'restart instance, reattach to running job, cancel it (long run)' '
	flux start --setattr=statedir=$(pwd) \
	     sh -c "flux job eventlog $(cat id1.out) > eventlog_long1.out; \
		    flux jobs -n > jobs_long1.out; \
		    flux cancel $(cat id1.out)" &&
	grep "reattach-start" eventlog_long1.out &&
	grep "reattach-finish" eventlog_long1.out &&
	grep $(cat id1.out) jobs_long1.out
'

test_expect_success 'restart instance, job completed (long run)' '
	flux start --setattr=statedir=$(pwd) \
	     sh -c "flux job eventlog $(cat id1.out) > eventlog_long2.out; \
		    flux jobs -n > jobs_long2.out" &&
	grep "finish" eventlog_long2.out | grep status &&
	test_must_fail grep $(cat id1.out) jobs_long2.out
'

# reattach_finish will indicate to testexec that the job finished
# right after reattach, emulating a job that finished before the
# instance restarted
test_expect_success 'run a testexec job in persistent instance (exit run)' '
	flux start --setattr=statedir=$(pwd) \
	     flux submit \
	       --flags=debug \
	       --setattr=system.exec.test.reattach_finish=1 \
	       --setattr=system.exec.test.run_duration=100s \
	     hostname >id2.out
'

test_expect_success 'restart instance, reattach to running job, its finished (exit run)' '
	flux start --setattr=statedir=$(pwd) \
	     sh -c "flux job eventlog $(cat id2.out) > eventlog_exit1.out" &&
	grep "reattach-start" eventlog_exit1.out &&
	grep "reattach-finish" eventlog_exit1.out &&
	grep "finish" eventlog_exit1.out | grep status
'

# The real (bulk-exec) executor does not implement reattach.  A job that is
# still running across a restart should get a fatal exception rather than
# relaunching its shells.  --input=/dev/null avoids the shell's KVS stdin
# watcher, which would otherwise fail (and fail the job) when the first
# instance tears down job-info, racing the reattach in the second instance.
test_expect_success 'run a real job and wait for it to start' '
	mkdir -p statedir_real &&
	flux start --setattr=statedir=$(pwd)/statedir_real \
	     flux submit --flags=debug --input=/dev/null \
	                 --wait-event=start sleep 300 >id3.out
'

test_expect_success 'restart instance, bulk-exec reattach raises exception' '
	flux start --setattr=statedir=$(pwd)/statedir_real \
	     sh -c "flux job wait-event -t 60 \$(cat id3.out) exception && \
	            flux job eventlog \$(cat id3.out) >eventlog_real1.out" &&
	grep "exception" eventlog_real1.out | grep "type=\"exec\"" &&
	grep "reattach to running job is not implemented" eventlog_real1.out
'

# A running job's guest namespace is grafted into the primary namespace at
# job.<id>.guest on shutdown, which must keep it reachable from the primary
# root so it is preserved when the KVS is dumped and restored across a
# restart.  Unlike online gc, dump only archives blobs reachable from the
# checkpoint root -- there is no epoch recency exemption -- so a canary
# written into the guest namespace survives the restart only if the graft
# keeps it reachable.
test_expect_success 'run a job, write a canary into its guest namespace, dump' '
	flux start -Scontent.dump=guestns.tar \
	     sh -c "flux submit --flags=debug \
	                --setattr=system.exec.test.run_duration=100s \
	                hostname >id4.out && \
	            flux job wait-event -t 60 \$(cat id4.out) start && \
	            flux kvs put -N job-\$(flux job id --to=dec \$(cat id4.out)) \
	                warmstart.canary=hello-4201" &&
	test -f guestns.tar
'

test_expect_success 'guest namespace survives dump/restore across restart' '
	flux start -Scontent.restore=guestns.tar \
	     sh -c "flux kvs get \
	                \$(flux job id --to=kvs \$(cat id4.out)).guest.warmstart.canary \
	                >canary.out" &&
	grep hello-4201 canary.out
'

test_done
