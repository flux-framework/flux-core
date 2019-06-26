#!/bin/sh

test_description='Test flux job execution service with dummy job shell'

. $(dirname $0)/sharness.sh

jq=$(which jq 2>/dev/null)
if test -z "$jq" ; then
	skip_all 'no jq found, skipping all tests'
	test_done
fi

test_under_flux 4 job

flux setattr log-stderr-level 1

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

job_kvsdir()    { flux job id --to=kvs $1; }
exec_eventlog() { flux kvs get -r $(job_kvsdir $1).guest.exec.eventlog; }

test_expect_success 'job-exec: load job-exec,sched-simple modules' '
	#  Add fake by_rank configuration to kvs:
	flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
	flux module load -r 0 sched-simple &&
	flux module load -r 0 job-exec
'
test_expect_success 'job-exec: no configured job shell uses /bin/true' '
	id=$(flux jobspec srun hostname | flux job submit) &&
	flux job attach ${id} &&
	flux dmesg | grep "$id: no configured job shell, using /bin/true"
'
test_expect_success 'job-exec: set dummy test job shell' '
	flux setattr job-exec.job-shell $SHARNESS_TEST_SRCDIR/job-exec/dummy.sh
'
test_expect_success 'job-exec: execute dummy job shell across all ranks' '
	id=$(flux jobspec srun -N4 \
	    "flux kvs put test1.\$BROKER_RANK=\$JOB_SHELL_RANK" \
	    | flux job submit) &&
	flux job wait-event $id clean &&
	kvsdir=$(flux job id --to=kvs $id).guest &&
	test $(flux kvs get ${kvsdir}.test1.0) = 0 &&
	test $(flux kvs get ${kvsdir}.test1.1) = 1 &&
	test $(flux kvs get ${kvsdir}.test1.2) = 2 &&
	test $(flux kvs get ${kvsdir}.test1.3) = 3
'
test_expect_success 'job-exec: job shell output sent to flux log' '
	id=$(flux jobspec srun -n 1 "echo Hello from job \$JOBID" \
	     | flux job submit) &&
	flux job wait-event $id clean &&
	flux dmesg | grep "Hello from job $id"
'
test_expect_success 'job-exec: job shell failure recorded' '
	id=$(flux jobspec srun -N4  "test \$JOB_SHELL_RANK = 0 && exit 1" \
	     | flux job submit) &&
	flux job wait-event -vt 1 $id finish | grep status=256
'
test_expect_success 'job-exec: status is maximum job shell exit codes' '
	id=$(flux jobspec srun -N4 "exit \$JOB_SHELL_RANK" | flux job submit) &&
	flux job wait-event -vt 1 $id finish | grep status=768
'
test_expect_success 'job-exec: job exception kills job shells' '
	id=$(flux jobspec srun -N4 sleep 300 | flux job submit) &&
	flux job wait-event -vt 1 $id start &&
	flux job cancel $id &&
	flux job wait-event -vt 1 $id clean &&
	flux job eventlog $id | grep status=9
'
test_expect_success 'job-exec: invalid job shell generates exception' '
	id=$(flux jobspec srun -N1 /bin/true \
	     | $jq ".attributes.system.exec.job_shell = \"/notthere\"" \
	     | flux job submit) &&
	flux job wait-event -vt 1 $id clean
'
test_expect_success 'job-exec: unload job-exec & sched-simple modules' '
	flux module remove job-exec &&
	flux module remove sched-simple
'
test_done
