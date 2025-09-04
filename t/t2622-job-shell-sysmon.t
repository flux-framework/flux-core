#!/bin/sh
#
test_description='Test flux-shell sysmon plugin'

. `dirname $0`/sharness.sh

cgdir=/sys/fs/cgroup/$(cat /proc/$$/cgroup | grep ^0: | cut -d: -f3)
if test $? -ne 0; then
        skip_all="cgroups is unavailable"
        test_done
fi
if ! test -e $cgdir/memory.current || ! test -e $cgdir/memory.peak; then
	skip_all="memory.current and/or memory.peak are unavailable"
	test_done
fi
if ! test -e $cgdir/cpu.stat; then
	skip_all="cpu.stat is unavailable"
	test_done
fi


test_under_flux 1

test_expect_success 'sysmon plugin is not loaded by default' '
	flux run true >disable.out &&
	test_must_fail grep sysmon disable.out
'
test_expect_success 'sysmon logs usage summary' '
	flux run -o sysmon true 2>enable.err &&
	grep "sysmon: memory.peak=" enable.err &&
	grep "sysmon: loadavg-overall=" enable.err
'
test_expect_success 'reload rank 0 heartbeat with fast period' '
	flux module reload heartbeat period=0.1s
'
test_expect_success 'sysmon trace logs current usage - heartbeat sync' '
	flux run -o verbose=2 -o sysmon sleep 1 2>tracehb.err &&
	grep "sysmon: memory.current=" tracehb.err &&
	grep "sysmon: loadavg=" tracehb.err
'
test_expect_success 'sysmon trace logs current usage - timer sync' '
	flux run -o verbose=2 -o sysmon.period=0.1s sleep 1 2>tracetimer.err &&
	grep "sysmon: memory.current=" tracetimer.err &&
	grep "sysmon: loadavg=" tracetimer.err
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
