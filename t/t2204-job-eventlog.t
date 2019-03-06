#!/bin/sh

test_description='Test flux job eventlog service'

. `dirname $0`/kvs/kvs-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

wait_lookups_nonzero() {
        i=0
        while (! flux module stats --parse lookups job-eventlog > /dev/null 2>&1 \
               || [ "$(flux module stats --parse lookups job-eventlog 2> /dev/null)" = "0" ]) \
              && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

test_expect_success 'sched-simple: load kvs-watch & job-eventlog module' '
	flux module load -r all kvs-watch &&
	flux module load -r all job-eventlog
'

test_expect_success 'flux job eventlog works (active)' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
	flux job eventlog $jobid > eventlog_a.out &&
        grep submit eventlog_a.out
'

test_expect_success 'flux job eventlog works on multiple entries (active)' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
        kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foo &&
	flux job eventlog $jobid >eventlog_b.out &&
	grep -q submit eventlog_b.out &&
	grep -q foo eventlog_b.out
'

# we cheat and manually move active to inactive in these tests

test_expect_success 'flux job eventlog works (inactive)' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/') &&
        flux kvs move ${activekvsdir}.eventlog ${inactivekvsdir}.eventlog &&
	flux job eventlog $jobid > eventlog_c.out &&
        grep submit eventlog_c.out
'

test_expect_success 'flux job eventlog works on multiple entries (inactive)' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${activekvsdir}.eventlog foo &&
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/') &&
        flux kvs move ${activekvsdir}.eventlog ${inactivekvsdir}.eventlog &&
	flux job eventlog $jobid >eventlog_d.out &&
	grep -q submit eventlog_d.out &&
	grep -q foo eventlog_d.out
'

test_expect_success 'flux job eventlog works on multiple entries (active -> inactive)' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/') &&
        flux kvs move ${activekvsdir}.eventlog ${inactivekvsdir}.eventlog &&
	flux kvs eventlog append ${inactivekvsdir}.eventlog foo &&
	flux job eventlog $jobid >eventlog_e.out &&
	grep -q submit eventlog_e.out &&
	grep -q foo eventlog_e.out
'

test_expect_success NO_CHAIN_LINT 'flux job eventlog --watch --count=N works (active)' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
        flux job eventlog --watch --count=4 $jobid > eventlog_watch_a.out &
        waitpid=$! &&
        wait_lookups_nonzero &&
        wait_watcherscount_nonzero primary &&
        kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foo &&
	flux kvs eventlog append ${kvsdir}.eventlog bar &&
	flux kvs eventlog append ${kvsdir}.eventlog baz &&
        wait $waitpid &&
        grep submit eventlog_watch_a.out &&
        grep foo eventlog_watch_a.out &&
        grep bar eventlog_watch_a.out &&
        grep baz eventlog_watch_a.out
'

test_expect_success NO_CHAIN_LINT 'flux job eventlog --watch --count=N works (active -> inactive) ' '
        jobid=$(flux jobspec --format json srun -N1 hostname | flux job submit)
        flux job eventlog --watch --count=4 $jobid > eventlog_watch_b.out &
        waitpid=$! &&
        wait_lookups_nonzero &&
        wait_watcherscount_nonzero primary &&
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${activekvsdir}.eventlog foo &&
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/') &&
        flux kvs move ${activekvsdir}.eventlog ${inactivekvsdir}.eventlog &&
	flux kvs eventlog append ${inactivekvsdir}.eventlog bar &&
	flux kvs eventlog append ${inactivekvsdir}.eventlog baz &&
        wait $waitpid &&
        test $(grep submit eventlog_watch_b.out | wc -l) -eq 1 &&
        test $(grep foo eventlog_watch_b.out | wc -l) -eq 1 &&
        test $(grep bar eventlog_watch_b.out | wc -l) -eq 1 &&
        test $(grep baz eventlog_watch_b.out | wc -l) -eq 1
'

test_expect_success 'job-eventlog stats works' '
        flux module stats job-eventlog | grep "lookups"
'
test_expect_success 'sched-simple: remove kvs-watch, sched-simple' '
        flux module remove -r all kvs-watch &&
        flux module remove -r all job-eventlog
'
test_done
