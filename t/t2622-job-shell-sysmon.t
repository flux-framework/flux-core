#!/bin/sh
#
test_description='Test flux-shell sysmon plugin'

. `dirname $0`/sharness.sh

cgdir=/sys/fs/cgroup/$(cat /proc/$$/cgroup | grep ^0: | cut -d: -f3)
if test $? -ne 0; then
        skip_all="incompatible cgroup configuration"
        test_done
fi
if test -e $cgdir/memory.current && test -e $cgdir/memory.peak; then
	test_set_prereq MEMORY
fi
if test -e $cgdir/cpu.stat; then
	test_set_prereq CPU
fi

test_under_flux 1

test_expect_success 'sysmon plugin is not loaded by default' '
	flux run true >disable.out &&
	test_must_fail grep sysmon disable.out
'
test_expect_success 'sysmon logs usage summary' '
	flux run -o sysmon true 2>enable.err
'
test_expect_success MEMORY 'sysmon logs memory.peak' '
	grep "sysmon: memory.peak=" enable.err
'
test_expect_success CPU 'sysmon logs loadavg' '
	grep "sysmon: loadavg-overall=" enable.err
'
test_expect_success 'reload rank 0 heartbeat with fast period' '
	flux module reload heartbeat period=0.1s
'
test_expect_success 'sysmon trace logs current usage - heartbeat sync' '
	flux run -o verbose=2 -o sysmon sleep 1 2>tracehb.err
'
test_expect_success CPU 'sysmon logs loadavg' '
	grep "sysmon: loadavg=" tracehb.err
'
test_expect_success MEMORY 'sysmon logs memory.current' '
	grep "sysmon: memory.current=" tracehb.err
'
test_expect_success 'sysmon trace logs current usage - timer sync' '
	flux run -o verbose=2 -o sysmon.period=0.1s sleep 1 2>tracetimer.err
'
test_expect_success CPU 'sysmon logs loadavg' '
	grep "sysmon: loadavg=" tracetimer.err
'
test_expect_success MEMORY 'sysmon logs memory.current' '
	grep "sysmon: memory.current=" tracetimer.err
'
test_expect_success '-o sysmon.period=30 is accepted as a valid FSD' '
	flux run -o sysmon.period=30 true
'
test_expect_success '-o sysmon.period=nonfsd fails' '
	test_must_fail flux run -o sysmon.period=nonfsd true 2>nonfsd.err &&
	grep "not a valid FSD" nonfsd.err
'
test_expect_success '-o sysmon.badopt fails' '
	test_must_fail flux run -o sysmon.badopt true 2>badopt.err &&
	grep badopt badopt.err
'

test_done
