#!/bin/sh

test_description='Test flux cron service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=4
test_under_flux ${SIZE} minimal

flux setattr log-stderr-level 1

cron_entry_check() {
    local id=$1
    local key=$2
    local expected="$3"
    test -n $id || return 1
    test -n $key || return 1
    test -n "$expected" || return 1
    local result="$(flux cron dump --key=${key} ${id})" || return 1
    echo "cron-${id}: ${key}=${result}, wanted ${expected}" >&2
    test "${result}" = "${expected}"
}

flux_cron() {
    result=$(flux cron "$@" 2>&1) || return 1
    id=$(echo ${result} | sed 's/^.*cron-\([0-9][0-9]*\).*/\1/')
    test -n "$id" && echo ${id}
}

test_expect_success 'load cron module' '
    flux module load cron
'
test_expect_success 'cron interval create' '
    id=$(flux_cron interval 0.05s echo cron-test-xxx) &&
    sleep .1 &&
    flux cron stop ${id} &&
    flux dmesg | grep "cron-1.*test-xxx"  &&
    cron_entry_check ${id} stopped true
'
test_expect_success 'cron list' '
    flux cron list | grep "^ *1.*echo"
'
test_expect_success 'cron delete' '
    flux cron delete 1 > delete.out &&
    grep "Removed cron-1: echo last ran" delete.out &&
    test_expect_code 1 flux cron dump 1
'
test_expect_success 'cron interval --after= works' '
    id=$(flux_cron interval --after=.01s 0 hostname) &&
    sleep .1 &&
    flux cron dump ${id} &&
    cron_entry_check ${id} task.1.state Exited
'

wait_cron_delete () {
    i=0
    flux cron dump ${id}
    while [ $? -eq 0 ] && [ $i -lt 50 ]
    do
        sleep 0.1
        i=$((i + 1))
        flux cron dump ${id}
    done
    if [ "$i" -eq "50" ]
    then
        return 1
    fi
    return 0;
}

test_expect_success 'cron delete leaves running task - --kill works' '
    id=$(flux_cron interval --after=.01s 0 sleep 100) &&
    sleep .1 &&
    cron_entry_check ${id} task.1.state Running &&
    flux cron delete ${id} > delete.${id}.out &&
    grep "sleep still running" delete.${id}.out &&
    cron_entry_check ${id} task.1.state Running &&
    flux cron delete --kill ${id} &&
    wait_cron_delete
'
test_expect_success 'repeat count works' '
    id=$(flux_cron interval -c1 .01s echo hi) &&
    sleep .02 &&
    cron_entry_check ${id} repeat 1 &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} stopped true
'
test_expect_success 'restarted job restarts repeat count' '
    id=$(flux_cron interval -c1 .01s echo repeat-count-check) &&
    sleep .1 &&
    cron_entry_check ${id} stopped true &&
    test $(flux dmesg | grep -c repeat-count-check) = 1 &&
    flux dmesg -c &&
    flux cron start ${id} &&
    sleep .1 &&
    test $(flux dmesg | grep -c repeat-count-check) = 1
'
test_expect_success 'rank option works' '
    id=$(flux_cron interval -c1 -o rank=1 .01s flux getattr rank) &&
    sleep .1 &&
    cron_entry_check ${id} stopped true &&
    cron_entry_check ${id} rank 1 &&
    flux dmesg | grep "cron-${id}.*command=\"flux getattr rank\": \"1\""
'
test_expect_success '--preserve-env option works' '
    export FOO=bar &&
    id=$(flux_cron interval --preserve-env -c1 -o rank=1 .01s printenv FOO) &&
    unset FOO &&
    sleep .1 &&
    cron_entry_check ${id} stopped true &&
    flux dmesg | grep "cron-${id}.*command=\"printenv FOO\": \"bar\""
'
test_expect_success '--working-dir option works' '
    id=$(flux_cron interval -c1 -d /tmp .01s pwd) &&
    sleep .1 &&
    cron_entry_check ${id} stopped true &&
    flux dmesg | grep "cron-${id}.*command=\"pwd\": \"/tmp\""
'

test_expect_success 'cron entry exec failure is recorded' '
    id=$(flux_cron interval -c1 0.01s notaprogram) &&
    sleep 0.1 &&
    test_debug "flux cron dump ${id} >&2" &&
    cron_entry_check ${id} stopped true &&
    cron_entry_check ${id} task.1.state "Failed" &&
    cron_entry_check ${id} task.1.code 127
'
test_expect_success 'cron entry launch failure recorded' '
    id=$(flux_cron interval -o rank=99 -c1 0.01s hostname) &&
    sleep 0.1 &&
    test_debug "flux cron dump ${id} >&2" &&
    cron_entry_check ${id} stopped true &&
    cron_entry_check ${id} task.1.state "Rexec Failure" &&
    cron_entry_check ${id} task.1.rexec_errno 113
'
test_expect_success 'flux-cron event works' '
    id=$(flux_cron event t.cron.trigger flux event pub t.cron.complete) &&
    cron_entry_check ${id} type event &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} stats.count 0 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.complete \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} task.1.state Exited &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.complete \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 2 &&
    cron_entry_check ${id} task.1.state Exited &&
    flux cron stop ${id} &&
    cron_entry_check ${id} stopped true &&
    flux cron delete ${id} &&
    test_expect_code 1 flux cron dump ${id}
'
test_expect_success 'flux-cron event --nth works' '
    id=$(flux_cron event --nth=3 t.cron.trigger flux event pub t.cron.complete) &&
    test_when_finished "flux cron delete ${id}" &&
    cron_entry_check ${id} type event &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.nth 3 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.counter 1 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.counter 2 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.complete \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} typedata.counter 3 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} typedata.counter 4
'

test_expect_success 'flux-cron event --after works' '
    id=$(flux_cron event --after=3 t.cron.trigger flux event pub t.cron.complete) &&
    test_when_finished "flux cron delete ${id}" &&
    cron_entry_check ${id} type event &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.after 3 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.counter 1 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.counter 2 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.complete \
        flux event pub t.cron.trigger &&
    flux cron dump ${id} &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} typedata.counter 3 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} typedata.counter 4 &&
    cron_entry_check ${id} stats.count 2
'
test_expect_success 'flux-cron event --min-interval works' '
    id=$(flux_cron event --min-interval=.5s t.cron.trigger hostname) &&
    test_when_finished "flux cron delete ${id}" &&
    cron_entry_check ${id} type event &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} stats.count 0 &&
    cron_entry_check ${id} typedata.min_interval 0.5 &&
    flux event pub t.cron.trigger && flux event pub t.cron.trigger &&
    cron_entry_check ${id} stats.count 1 &&
    sleep 0.5 &&
    cron_entry_check ${id} stats.count 2
'
test_expect_success 'flux-cron can set timeout on tasks' '
    id=$(flux_cron event -o timeout=0.1 t.cron.trigger sleep 120) &&
    test_when_finished "flux cron delete ${id}" &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    sleep 0.1 &&
    i=0 &&
    while test $i -lt 5; do
        cron_entry_check ${id} task.1.state Timeout
	rc=$?
        if test $rc -eq 0; then break; fi
	sleep 0.1
        i=$((i+1))
	echo "cron-${id}: $i"
    	flux cron dump ${id}
    done &&
    test $rc -eq 0
'
test_expect_success 'flux-cron can set stop-on-failure' '
    id=$(flux_cron event -o stop-on-failure=3 t2.cron.trigger \
         "flux event pub t2.cron.complete && false" ) &&
    cron_entry_check ${id} type event &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} stats.count 0 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t2.cron t2.cron.complete \
        flux event pub t2.cron.trigger &&
    flux cron dump ${id} &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} stats.failure 1 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t2.cron t2.cron.complete \
        flux event pub t2.cron.trigger &&
    cron_entry_check ${id} stats.count 2 &&
    cron_entry_check ${id} stats.failure 2 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t2.cron t2.cron.complete \
        flux event pub t2.cron.trigger &&
    cron_entry_check ${id} stats.count 3 &&
    cron_entry_check ${id} stats.failure 3 &&
    cron_entry_check ${id} stopped true
'

##  Reload cron module with sync enabled
test_expect_success 'flux module remove cron' '
    flux module remove cron
'
test_expect_success 'module load with sync' '
    flux module load cron sync=cron.sync sync_epsilon=0.025
'
test_expect_success 'sync and sync_epsilon are set as expected' '
    flux cron sync | grep "cron\.sync.*epsilon=0.025"
'
test_expect_success 'tasks do not run until sync event' '
    id=$(flux_cron event t.cron.trigger flux event pub t.cron.complete) &&
    test_when_finished "flux cron delete ${id}" &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} stats.count 0 &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.trigger \
        flux event pub t.cron.trigger &&
    cron_entry_check ${id} task.1.state Deferred &&
    $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua t.cron t.cron.complete \
        flux event pub cron.sync &&
    cron_entry_check ${id} stats.count 1
'
test_expect_success 'flux cron sync can disable sync' '
    flux cron sync --disable &&
    flux cron sync | grep disabled
'
test_expect_success 'flux cron sync can enable sync' '
    flux cron sync cron.sync2 &&
    flux cron sync | grep cron.sync2
'
test_expect_success 'flux cron sync can set epsilon' '
    flux cron sync --epsilon=42s cron.sync2 &&
    flux cron sync | grep 42.000s
'
test_expect_success 'flux module remove cron' '
    flux module remove cron
'
test_done
