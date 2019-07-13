#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

PMI_INFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
KVSTEST=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'job-shell: load barrier,job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load barrier &&
        flux module load -r 0 sched-simple &&
        flux module load -r 0 job-exec
'
test_expect_success 'job-shell: execute across all ranks' '
        id=$(flux jobspec srun -N4 bash -c \
            "flux kvs put test1.\$FLUX_TASK_RANK=\$FLUX_TASK_LOCAL_ID" \
            | flux job submit) &&
        flux job attach --show-events $id &&
        kvsdir=$(flux job id --to=kvs $id).guest &&
        test $(flux kvs get ${kvsdir}.test1.0) = 0 &&
        test $(flux kvs get ${kvsdir}.test1.1) = 0 &&
        test $(flux kvs get ${kvsdir}.test1.2) = 0 &&
        test $(flux kvs get ${kvsdir}.test1.3) = 0
'
test_expect_success 'job-shell: execute 2 tasks per rank' '
        id=$(flux jobspec srun -N4 -n8 bash -c \
            "flux kvs put test2.\$FLUX_TASK_RANK=\$FLUX_TASK_LOCAL_ID" \
            | flux job submit) &&
        flux job attach --show-events $id &&
        kvsdir=$(flux job id --to=kvs $id).guest &&
        test $(flux kvs get ${kvsdir}.test2.0) = 0 &&
        test $(flux kvs get ${kvsdir}.test2.1) = 1 &&
        test $(flux kvs get ${kvsdir}.test2.2) = 0 &&
        test $(flux kvs get ${kvsdir}.test2.3) = 1 &&
        test $(flux kvs get ${kvsdir}.test2.4) = 0 &&
        test $(flux kvs get ${kvsdir}.test2.5) = 1 &&
        test $(flux kvs get ${kvsdir}.test2.6) = 0 &&
        test $(flux kvs get ${kvsdir}.test2.7) = 1
'
test_expect_success 'job-shell: /bin/true exit code propagated' '
        id=$(flux jobspec srun -n1 /bin/true | flux job submit) &&
	flux job wait-event $id finish >true.finish.out &&
	grep status=0 true.finish.out
'
test_expect_success 'job-shell: /bin/false exit code propagated' '
        id=$(flux jobspec srun -n1 /bin/false | flux job submit) &&
	flux job wait-event $id finish >false.finish.out &&
	grep status=256 false.finish.out
'
test_expect_success 'job-shell: PMI works' '
        id=$(flux jobspec srun -N4 ${PMI_INFO} | flux job submit) &&
	flux job wait-event $id finish >pmi_info.out 2>pmi_info.err &&
	flux dmesg | grep kvsname= | grep size=4 >pmi_info.dmesg
'
test_expect_success 'job-shell: PMI KVS works' '
        id=$(flux jobspec srun -N4 ${KVSTEST} | flux job submit) &&
	flux job wait-event $id finish >kvstest.out 2>kvstest.err &&
	flux dmesg | grep "t phase" >kvstest.dmesg
'
test_expect_success 'job-exec: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
