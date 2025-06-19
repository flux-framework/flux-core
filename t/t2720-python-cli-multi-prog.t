#!/bin/sh

test_description='flux multi-prog'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
test_under_flux 4 job

flux setattr log-stderr-level 1

test_expect_success 'flux-multi-prog prints usage with no args' '
	test_expect_code 2 flux multi-prog 2>no-args.err &&
	grep usage no-args.err
'
test_expect_success 'flux-multi-prog --help works' '
	flux multi-prog --help >help.out &&
	grep CONFIG help.out
'
test_expect_success 'flux-multi-prog raises error with bad config file' '
	test_must_fail flux multi-prog missing.conf 2>missing.err &&
	test_debug "cat missing.err" &&
	grep "No such file" missing.err 
'
test_expect_success 'flux-multi-prog: basic config' '
	name=basic &&
	cat <<-EOF >${name}.conf &&
	# srun docs silly config
	4-6	hostname
	1,7	echo task%t
	0,2-3	echo offset:%o
	*	echo all task=%t
	EOF
	cat <<-EOF2 >${name}.expected &&
	0: echo offset:0
	1: echo task1
	2: echo offset:1
	3: echo offset:2
	4: hostname
	5: hostname
	6: hostname
	7: echo task7
	8: echo all task=8
	9: echo all task=9
	EOF2
	flux multi-prog -n 0-9 ${name}.conf >${name}.out &&
	test_debug "cat ${name}.out" &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-multi-prog: position of "*" does not matter' '
	name=basic &&
	cat <<-EOF >${name}.conf &&
	# srun docs silly config
	*	echo all task=%t
	4-6	hostname
	1,7	echo task%t
	0,2-3	echo offset:%o
	EOF
	cat <<-EOF2 >${name}.expected &&
	0: echo offset:0
	1: echo task1
	2: echo offset:1
	3: echo offset:2
	4: hostname
	5: hostname
	6: hostname
	7: echo task7
	8: echo all task=8
	9: echo all task=9
	EOF2
	flux multi-prog -n 0-9 ${name}.conf >${name}.out &&
	test_debug "cat ${name}.out" &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-multi-prog: command and args accept quoting' '
	name=quoting &&
	cat <<-"EOF" >${name}.conf &&
	0-1 echo "foo bar" %t # line comment for good measure
	EOF
	cat <<-EOF >${name}.expected &&
	0: echo '"'"'foo bar'"'"' 0
	1: echo '"'"'foo bar'"'"' 1
	EOF
	flux multi-prog -n 0-1 ${name}.conf >${name}.out &&
	test_debug "cat ${name}.out" &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-multi-prog: missing tasks raise error' '
	name=missing-task &&
	cat <<-EOF >${name}.conf &&
	0-1 foo
	EOF
	test_must_fail flux multi-prog -n 0-3 ${name}.conf 2>${name}.err &&
	test_debug "cat ${name}.err" &&
	grep "No matching line for rank 2" ${name}.err
'
test_expect_success 'flux-multi-prog: invalid idset raises error' '
	name=bad-line &&
	cat <<-EOF >${name}.conf &&
	# good line:
	0-1 hostname
	# bad line:
	1-0 hostname
	EOF
	test_must_fail flux multi-prog -n 0-1 ${name}.conf 2>${name}.err &&
	test_debug "cat ${name}.err" &&
	grep "line 4: invalid idset: 1-0" ${name}.err
'
test_expect_success 'flux-multi-prog: invalid line raises error' '
	name=bad-line2 &&
	cat <<-EOF >${name}.conf &&
	0-1 echo "foo bar baz
	EOF
	test_must_fail flux multi-prog -n 0-1 ${name}.conf 2>${name}.err &&
	test_debug "cat ${name}.err" &&
	grep "No closing quotation" ${name}.err
'
test_expect_success 'flux-multi-prog uses FLUX_TASK_RANK by default' '
	FLUX_TASK_RANK=7 flux multi-prog basic.conf >env.out &&
	test_debug "cat env.out" &&
	test "$(cat env.out)" = "task7"
'
test_expect_success 'flux-multi-prog works under flux-run' '
	flux run -n8 --label-io flux multi-prog basic.conf >run.out &&
	test_debug "cat run.out" &&
	grep "7: task7" run.out &&
	grep "0: offset:0" run.out &&
	grep "3: offset:2" run.out
'
test_done
