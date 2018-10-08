#!/bin/sh

test_description='Test flux job manager service'

. $(dirname $0)/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi
if flux job submitbench --help 2>&1 | grep -q sign-type; then
    test_set_prereq HAVE_FLUX_SECURITY
    SUBMITBENCH_OPT_R="--reuse-signature"
    SUBMITBENCH_OPT_NONE="--sign-type=none"
fi

test_under_flux 4 job

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
SUBMITBENCH="flux job submitbench $SUBMITBENCH_OPT_NONE"

# N.B. job submission and job list are eventually consistent
# Wait here for all jobs submitted to be in the queue before proceeding.
test_expect_success 'job-manager: submit jobs and list queue' '
	${SUBMITBENCH} -p0 ${JOBSPEC}/valid/basic.yaml >submit1_p0.out &&
	${SUBMITBENCH} -p1 ${JOBSPEC}/valid/basic.yaml >submit1_p1.out &&
	${SUBMITBENCH} -r 100 ${JOBSPEC}/valid/basic.yaml \
			| sort -n >submit100_p16.out &&
	${SUBMITBENCH} -p20 ${JOBSPEC}/valid/basic.yaml >submit1_p20.out &&
	${SUBMITBENCH} -p31 ${JOBSPEC}/valid/basic.yaml >submit1_p31.out &&
	tries=3 &&
	while test $tries -gt 0; do \
		flux job list -s >list.out; \
		count=$(wc -l <list.out); \
		test $count -lt 104 || break; \
		tries=$(($tries-1)); \
	done &&
	test $count -eq 104
'

test_expect_success 'job-manager: flux job list shows priority=31 job first' '
	head -1 <list.out | cut -f1 >top.out &&
	test_cmp submit1_p31.out top.out
'

test_expect_success 'job-manager: flux job list shows priority=0 job last' '
	tail -1 <list.out | cut -f1 >last.out &&
	test_cmp submit1_p0.out last.out
'

test_expect_success 'job-manager: flux job list shows priority=20 job second' '
	head -2 <list.out | tail -1 | cut -f1 >second.out &&
	test_cmp submit1_p20.out second.out
'

test_expect_success 'job-manager: flux job list shows priority=1 job next to last' '
	tail -2 <list.out | head -1 | cut -f1 >secondlast.out &&
	test_cmp submit1_p1.out secondlast.out
'

test_expect_success 'job-manager: flux job list shows expected priorities' '
	test $(head -1 <list.out | cut -f3) -eq 31 &&
	test $(tail -1 <list.out | cut -f3) -eq 0 &&
	test $(head -2 <list.out | tail -1 | cut -f3) -eq 20 &&
	test $(tail -2 <list.out | head -1 | cut -f3) -eq 1
'

test_expect_success 'job-manager: flux job list shows expected userid' '
	test $(head -1 <list.out | cut -f2) -eq $(id -u)
'

test_expect_success 'job-manager: flux job list --count shows highest priority jobs' '
	flux job list -s -c 4 >list4.out &&
	head -4 <list.out >list4.exp &&
	test_cmp list4.exp list4.out
'

test_done
