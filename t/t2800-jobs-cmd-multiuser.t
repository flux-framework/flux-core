#!/bin/sh

test_description='Test flux jobs command with multiple user jobs'

. $(dirname $0)/sharness.sh

test_under_flux 4 job -Slog-stderr-level=1


if ! flux version | grep -q libflux-security; then
    skip_all='libflux-security not built, skipping'
    test_done
fi

submit_job_altuser()
{
        USERID=$1
        flux run --dry-run --setattr=exec.test.run_duration=0.1s hostname | \
            flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $USERID \
                 >job.signed
        FLUX_HANDLE_USERID=$USERID \
            flux job submit --flags=signed job.signed
}

test_expect_success 'configure guest access to test exec' '
        flux config load <<-EOF
	[exec.testexec]
	allow-guests = true
	EOF
'

myid=$(id -u)
id1=$(($myid + 1))
id2=$(($myid + 2))

test_expect_success 'submit jobs as several different users' '
	jobid=$(submit_job_altuser $myid) &&
	flux job wait-event -t 30s $jobid clean &&
	jobid=$(submit_job_altuser $id1) &&
	flux job wait-event -t 30s $jobid clean &&
	jobid=$(submit_job_altuser $id2) &&
	flux job wait-event -t 30s $jobid clean
'
test_expect_success 'default only shows current user jobs' '
        flux jobs -a -n -o "{id},{userid}" > default.out &&
        test $(wc -l < default.out) -eq 1 &&
        grep $myid default.out
'
test_expect_success '-u option works with specific users' '
        flux jobs -a -n -u $id1 -o "{id},{userid}" > user1.out &&
        test $(wc -l < user1.out) -eq 1 &&
        grep $id1 user1.out &&
        flux jobs -a -n -u $id2 -o "{id},{userid}" > user2.out &&
        test $(wc -l < user2.out) -eq 1 &&
        grep $id2 user2.out
'
test_expect_success '-u all option shows all jobs' '
        flux jobs -a -n -u all -o "{id},{userid}" > userall.out &&
        test $(wc -l < userall.out) -eq 3
'
test_expect_success '-A option shows all jobs' '
        flux jobs -a -n -A -o "{id},{userid}" > optionA.out &&
        test $(wc -l < optionA.out) -eq 3
'
test_expect_success '--include defaults to work against all user jobs' '
        flux jobs -a -n -i $(hostname) -o "{id},{userid},{nodelist}" > includeall.out &&
        test $(wc -l < includeall.out) -eq 3
'
test_expect_success '--include w/ -u only lists user jobs' '
        flux jobs -a -n -i $(hostname) -u $id1 -o "{id},{userid}" > include1.out &&
        test $(wc -l < include1.out) -eq 1 &&
        grep $id1 include1.out &&
        flux jobs -a -n -i $(hostname) -u $id2 -o "{id},{userid}" > include2.out &&
        test $(wc -l < include2.out) -eq 1 &&
        grep $id2 include2.out
'
test_done
