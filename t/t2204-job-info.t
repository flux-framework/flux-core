#!/bin/sh

test_description='Test flux job info service'

. `dirname $0`/kvs/kvs-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 4 job

# Usage: submit_job
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.
submit_job() {
        jobid=$(flux job submit test.json)
        flux job cancel $jobid
        flux job wait-event $jobid clean >/dev/null
        echo $jobid
}

wait_watchers_nonzero() {
        i=0
        while (! flux module stats --parse watchers job-info > /dev/null 2>&1 \
               || [ "$(flux module stats --parse watchers job-info 2> /dev/null)" = "0" ]) \
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

# We cheat and manually move active to inactive in these tests.
move_inactive() {
        activekvsdir=$(flux job id --to=kvs-active $1)
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/')
        flux kvs move ${activekvsdir} ${inactivekvsdir}
        return 0
}

test_expect_success 'job-info: generate jobspec for simple test job' '
        flux jobspec --format json srun -N1 hostname > test.json
'

#
# job eventlog tests
#

test_expect_success 'flux job eventlog works (active)' '
        jobid=$(submit_job) &&
	flux job eventlog $jobid > eventlog_a.out &&
        grep submit eventlog_a.out
'

test_expect_success 'flux job eventlog works on multiple entries (active)' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foo &&
	flux job eventlog $jobid >eventlog_b.out &&
	grep -q submit eventlog_b.out &&
	grep -q foo eventlog_b.out
'

test_expect_success 'flux job eventlog works (inactive)' '
        jobid=$(submit_job) &&
        move_inactive $jobid &&
	flux job eventlog $jobid > eventlog_c.out &&
        grep submit eventlog_c.out
'

test_expect_success 'flux job eventlog works on multiple entries (inactive)' '
        jobid=$(submit_job) &&
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${activekvsdir}.eventlog foo &&
        move_inactive $jobid &&
	flux job eventlog $jobid >eventlog_d.out &&
	grep -q submit eventlog_d.out &&
	grep -q foo eventlog_d.out
'

test_expect_success 'flux job eventlog works on multiple entries (active -> inactive)' '
        jobid=$(submit_job) &&
        move_inactive $jobid &&
	flux kvs eventlog append ${inactivekvsdir}.eventlog foo &&
	flux job eventlog $jobid >eventlog_e.out &&
	grep -q submit eventlog_e.out &&
	grep -q foo eventlog_e.out
'

test_expect_success 'flux job eventlog fails on bad id' '
	! flux job eventlog 12345
'

test_expect_success 'flux job eventlog --format=json works' '
        jobid=$(submit_job) &&
	flux job eventlog --format=json $jobid > eventlog_format1.out &&
        grep -q "\"userid\":$(id -u)" eventlog_format1.out
'

test_expect_success 'flux job eventlog --format=text works' '
        jobid=$(submit_job) &&
	flux job eventlog --format=text $jobid > eventlog_format2.out &&
        grep -q "userid=$(id -u)" eventlog_format2.out
'

test_expect_success 'flux job eventlog --format=invalid fails' '
        jobid=$(submit_job) &&
	! flux job eventlog --format=invalid $jobid
'

#
# job wait-event tests
#

test_expect_success 'flux job wait-event works (active)' '
        jobid=$(submit_job) &&
        flux job wait-event $jobid submit > wait_event1.out &&
        grep submit wait_event1.out
'

test_expect_success 'flux job wait-event works (inactive)' '
        jobid=$(submit_job) &&
        move_inactive $jobid &&
        flux kvs eventlog append ${inactivekvsdir}.eventlog foobar &&
        flux job wait-event $jobid submit > wait_event2.out &&
        grep submit wait_event2.out
'

test_expect_success NO_CHAIN_LINT 'flux job wait-event works, event is later (active)' '
        jobid=$(submit_job)
        flux job wait-event $jobid foobar > wait_event3.out &
        waitpid=$! &&
        wait_watchers_nonzero &&
        wait_watcherscount_nonzero primary &&
        kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        wait $waitpid &&
        grep foobar wait_event3.out
'

# must carefully fake active -> inactive transition to avoid race
# where job-info module sees inactive transition but does not see any
# new events.  We do this by copying the eventlog, modifying the
# inactive eventlog, then removing the active one.

test_expect_success NO_CHAIN_LINT 'flux job wait-event works, event is later (active -> inactive) ' '
        jobid=$(submit_job)
        flux job wait-event $jobid foobar > wait_event4.out &
        waitpid=$! &&
        wait_watchers_nonzero &&
        wait_watcherscount_nonzero primary &&
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
        flux kvs eventlog append ${activekvsdir}.eventlog foobaz &&
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/') &&
        flux kvs copy ${activekvsdir}.eventlog ${inactivekvsdir}.eventlog &&
        flux kvs eventlog append ${inactivekvsdir}.eventlog foobar &&
        flux kvs unlink ${activekvsdir}.eventlog &&
        wait $waitpid &&
        grep foobar wait_event4.out
'

test_expect_success 'flux job wait-event exits if never receives event (inactive) ' '
        jobid=$(submit_job) &&
        move_inactive $jobid &&
        ! flux job wait-event $jobid foobar > wait_event5.out 2> wait_event5.err &&
        ! test -s wait_event5.out &&
        grep "never received" wait_event5.err
'

# can't move the entire active directory wholesale in this test,
# otherwise the move of a specific key will be missed

test_expect_success NO_CHAIN_LINT 'flux job wait-event exits if never receives event (active -> inactive) ' '
        jobid=$(submit_job)
        flux job wait-event $jobid foobar > wait_event6.out 2> wait_event6.err &
        waitpid=$! &&
        wait_watchers_nonzero &&
        wait_watcherscount_nonzero primary &&
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
        inactivekvsdir=$(echo $activekvsdir | sed 's/active/inactive/') &&
        flux kvs move ${activekvsdir}.eventlog ${inactivekvsdir}.eventlog &&
        ! wait $waitpid &&
        ! test -s wait_event6.out &&
        grep "never received" wait_event6.err
'

test_expect_success 'flux job wait-event fails on bad id' '
	! flux job wait-event 12345 foobar
'

test_expect_success 'flux job wait-event --quiet works' '
        jobid=$(submit_job) &&
        flux job wait-event --quiet $jobid submit > wait_event7.out &&
        ! test -s wait_event7.out
'

test_expect_success 'flux job wait-event --verbose works' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobaz &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        flux job wait-event --verbose $jobid foobar > wait_event8.out &&
        grep submit wait_event8.out &&
        grep foobaz wait_event8.out &&
        grep foobar wait_event8.out
'

test_expect_success 'flux job wait-event --verbose doesnt show events after wait event' '
        jobid=$(submit_job) &&
        kvsdir=$(flux job id --to=kvs-active $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foobar &&
        flux job wait-event --verbose $jobid submit > wait_event9.out &&
        grep submit wait_event9.out &&
        ! grep foobar wait_event9.out
'

test_expect_success 'flux job wait-event --timeout works' '
        jobid=$(submit_job) &&
        ! flux job wait-event --timeout=0.2 $jobid foobar 2> wait_event8.err &&
        grep "wait-event timeout" wait_event8.err
'

test_expect_success 'flux job wait-event hangs on no event' '
        jobid=$(submit_job) &&
        ! run_timeout 0.2 flux job wait-event $jobid foobar
'

test_expect_success 'flux job wait-event --format=json works' '
        jobid=$(submit_job) &&
	flux job wait-event --format=json $jobid submit > wait_event_format1.out &&
        grep -q "\"userid\":$(id -u)" wait_event_format1.out
'

test_expect_success 'flux job wait-event --format=text works' '
        jobid=$(submit_job) &&
	flux job wait-event --format=text $jobid submit > wait_event_format2.out &&
        grep -q "userid=$(id -u)" wait_event_format2.out
'

test_expect_success 'flux job wait-event --format=invalid fails' '
        jobid=$(submit_job) &&
	! flux job wait-event --format=invalid $jobid submit
'

#
# job info tests
#

test_expect_success 'flux job info eventlog works (active)' '
        jobid=$(submit_job) &&
	flux job info $jobid eventlog > eventlog_info_a.out &&
        grep submit eventlog_info_a.out
'

test_expect_success 'flux job info eventlog works (inactive)' '
        jobid=$(submit_job) &&
        move_inactive $jobid &&
	flux job info $jobid eventlog > eventlog_info_b.out &&
        grep submit eventlog_info_b.out
'

test_expect_success 'flux job info eventlog fails on bad id' '
	! flux job info 12345 eventlog
'

test_expect_success 'flux job info jobspec works (active)' '
        jobid=$(submit_job) &&
	flux job info $jobid jobspec > jobspec_a.out &&
        grep hostname jobspec_a.out
'

test_expect_success 'flux job info jobspec works (inactive)' '
        jobid=$(submit_job) &&
        move_inactive $jobid &&
	flux job info $jobid jobspec > jobspec_b.out &&
        grep hostname jobspec_b.out
'

test_expect_success 'flux job info jobspec fails on bad id' '
	! flux job info 12345 jobspec
'

#
# job info tests (multiple info requests)
#

test_expect_success 'flux job info multiple keys works (active)' '
        jobid=$(flux job submit test.json) &&
	flux job info $jobid eventlog jobspec J > all_info_a.out &&
        grep submit all_info_a.out &&
        grep hostname all_info_a.out
'

test_expect_success 'flux job info multiple keys works (inactive)' '
        jobid=$(flux job submit test.json) &&
        move_inactive $jobid &&
	flux job info $jobid eventlog jobspec J > all_info_b.out &&
        grep submit all_info_b.out &&
        grep hostname all_info_a.out
'

test_expect_success 'flux job info multiple keys fails on bad id' '
	! flux job info 12345 eventlog jobspec J
'

test_expect_success 'flux job info multiple keys fails on 1 bad entry (include eventlog)' '
        jobid=$(flux job submit test.json) &&
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
        flux kvs unlink ${activekvsdir}.jobspec &&
	! flux job info $jobid eventlog jobspec J > all_info_b.out
'

test_expect_success 'flux job info multiple keys fails on 1 bad entry (no eventlog)' '
        jobid=$(flux job submit test.json) &&
        activekvsdir=$(flux job id --to=kvs-active $jobid) &&
        flux kvs unlink ${activekvsdir}.jobspec &&
	! flux job info $jobid jobspec J > all_info_b.out
'

#
# stats
#

test_expect_success 'job-info stats works' '
        flux module stats job-info | grep "lookups" &&
        flux module stats job-info | grep "watchers"
'

test_done
