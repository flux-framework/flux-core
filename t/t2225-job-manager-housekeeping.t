#!/bin/sh
test_description='Test job manager housekeeping'

. $(dirname $0)/sharness.sh

test_under_flux 4

flux setattr log-stderr-level 1

waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

test_expect_success 'create housekeeping script' '
	cat >housekeeping.sh <<-EOT &&
	#!/bin/sh
	trashdir=\$1
	rank=\$(flux getattr rank)
	touch \$trashdir/hkflag.\$rank
	EOT
	chmod +x housekeeping.sh
'
test_expect_success 'configure basic housekeeping' '
	flux config load <<-EOT
	[job-manager.housekeeping]
	command = "$(pwd)/housekeeping.sh $(pwd)"
	EOT
'
test_expect_success 'run a job on broker ranks 1-2' '
	rm -f hkflag.* &&
	flux run -N2 --requires=ranks:1-2 /bin/true
'
test_expect_success 'housekeeping script ran on ranks 1-2' '
	$waitfile -t 30 hkflag.1 &&
	$waitfile -t 30 hkflag.2
'
test_expect_success 'configure housekeeping with immediate release' '
	flux config load <<-EOT
	[job-manager.housekeeping]
	command = "$(pwd)/housekeeping.sh $(pwd)"
	release-after = "0"
	EOT
'
test_expect_success 'run a job on all four ranks' '
	rm -f hkflag.* &&
	flux dmesg -C &&
	flux run -n8 -N4 /bin/true
'
test_expect_success 'housekeeping script ran on ranks 0-3' '
	$waitfile -t 30 hkflag.0 &&
	$waitfile -t 30 hkflag.1 &&
	$waitfile -t 30 hkflag.2 &&
	$waitfile -t 30 hkflag.3
'
test_expect_success 'nodes were returned to scheduler separately' '
	flux dmesg -H | grep sched-simple >sched.log &&
	grep "free: rank0" sched.log &&
	grep "free: rank1" sched.log &&
	grep "free: rank2" sched.log &&
	grep "free: rank3" sched.log
'
test_expect_success 'create housekeeping script with one 10s straggler' '
	cat >housekeeping2.sh <<-EOT &&
	#!/bin/sh
	trashdir=\$1
	rank=\$(flux getattr rank)
	test \$rank -eq 3 && sleep 10
	touch \$trashdir/hkflag.\$rank
	EOT
	chmod +x housekeeping2.sh
'
test_expect_success 'configure housekeeping with release after 5s' '
	flux config load <<-EOT
	[job-manager.housekeeping]
	command = "$(pwd)/housekeeping2.sh $(pwd)"
	release-after = "5s"
	EOT
'
test_expect_success 'run a job on all four ranks' '
	rm -f hkflag.* &&
	flux dmesg -C &&
	flux run -n8 -N4 /bin/true
'
test_expect_success 'housekeeping script ran on ranks 0-3' '
	$waitfile -t 30 hkflag.0 &&
	$waitfile -t 30 hkflag.1 &&
	$waitfile -t 30 hkflag.2 &&
	$waitfile -t 30 hkflag.3
'
test_expect_success 'there was one alloc and two frees to the scheduler' '
	flux dmesg -H | grep sched-simple >sched2.log &&
	grep "free: rank\[0-2\]" sched2.log &&
	grep "free: rank3" sched2.log
'
test_expect_success 'configuring housekeeping with bad key fails' '
	test_must_fail flux config load 2>load.err <<-EOT &&
	[job-manager.housekeeping]
	xyz = 42
	EOT
	grep "left unpacked" load.err
'
test_expect_success 'configuring housekeeping with bad fsd fails' '
	test_must_fail flux config load 2>load2.err <<-EOT &&
	[job-manager.housekeeping]
	command = "/bin/true"
	release-after = "foo"
	EOT
	grep "FSD parse error" load2.err
'
test_expect_success 'configure housekeeping with wrong path' '
	flux config load <<-EOT
	[job-manager.housekeeping]
	command = "/noexist"
	EOT
'
test_expect_success 'run a job and ensure error was logged' '
	flux dmesg -C &&
	flux run /bin/true &&
	flux dmesg | grep "error launching process"
'
test_done
