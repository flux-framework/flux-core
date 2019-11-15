#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

PMI_INFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
KVSTEST=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest
LPTEST=${SHARNESS_TEST_DIRECTORY}/shell/lptest

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'job-shell: load barrier,job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load barrier &&
        flux module load -r 0 sched-simple res=resource.hwloc.by_rank &&
        flux module load -r 0 job-exec
'
test_expect_success 'job-shell: execute across all ranks' '
        id=$(flux jobspec srun -N4 bash -c \
            "flux kvs put test1.\$FLUX_TASK_RANK=\$FLUX_TASK_LOCAL_ID" \
            | flux job submit) &&
        flux job attach --show-events $id &&
        kvsdir=$(flux job id --to=kvs $id) &&
	sort >test1.exp <<-EOT &&
	${kvsdir}.guest.test1.0 = 0
	${kvsdir}.guest.test1.1 = 0
	${kvsdir}.guest.test1.2 = 0
	${kvsdir}.guest.test1.3 = 0
	EOT
	flux kvs dir ${kvsdir}.guest.test1 | sort >test1.out &&
	test_cmp test1.exp test1.out
'
test_expect_success 'job-shell: execute 2 tasks per rank' '
        id=$(flux jobspec srun -N4 -n8 bash -c \
            "flux kvs put test2.\$FLUX_TASK_RANK=\$FLUX_TASK_LOCAL_ID" \
            | flux job submit) &&
        flux job attach --show-events $id &&
        kvsdir=$(flux job id --to=kvs $id) &&
	sort >test2.exp <<-EOT &&
	${kvsdir}.guest.test2.0 = 0
	${kvsdir}.guest.test2.1 = 1
	${kvsdir}.guest.test2.2 = 0
	${kvsdir}.guest.test2.3 = 1
	${kvsdir}.guest.test2.4 = 0
	${kvsdir}.guest.test2.5 = 1
	${kvsdir}.guest.test2.6 = 0
	${kvsdir}.guest.test2.7 = 1
	EOT
	flux kvs dir ${kvsdir}.guest.test2 | sort >test2.out &&
	test_cmp test2.exp test2.out
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
	flux job attach $id >pmi_info.out 2>pmi_info.err &&
	grep size=4 pmi_info.out
'
test_expect_success 'pmi-shell: PMI cliques are correct for 1 ppn' '
        id=$(flux jobspec srun -N4 -n4 ${PMI_INFO} -c | flux job submit) &&
	flux job attach $id >pmi_clique1.raw 2>pmi_clique1.err &&
	sort -snk1 <pmi_clique1.raw >pmi_clique1.out &&
	sort >pmi_clique1.exp <<-EOT &&
	0: clique=0
	1: clique=1
	2: clique=2
	3: clique=3
	EOT
	test_cmp pmi_clique1.exp pmi_clique1.out
'
test_expect_success 'pmi-shell: PMI cliques are correct for 2 ppn' '
        id=$(flux jobspec srun -N2 -n4 ${PMI_INFO} -c | flux job submit) &&
	flux job attach $id >pmi_clique2.raw 2>pmi_clique2.err &&
	sort -snk1 <pmi_clique2.raw >pmi_clique2.out &&
	sort >pmi_clique2.exp <<-EOT &&
	0: clique=0,1
	1: clique=0,1
	2: clique=2,3
	3: clique=2,3
	EOT
	test_cmp pmi_clique2.exp pmi_clique2.out
'
test_expect_success 'pmi-shell: PMI cliques are correct for irregular ppn' '
        id=$(flux jobspec srun -N4 -n5 ${PMI_INFO} -c | flux job submit) &&
	flux job attach $id >pmi_cliquex.raw 2>pmi_cliquex.err &&
	sort -snk1 <pmi_cliquex.raw >pmi_cliquex.out &&
	sort >pmi_cliquex.exp <<-EOT &&
	0: clique=0,1
	1: clique=0,1
	2: clique=2
	3: clique=3
	4: clique=4
	EOT
	test_cmp pmi_cliquex.exp pmi_cliquex.out
'
test_expect_success 'job-shell: PMI KVS works' '
        id=$(flux jobspec srun -N4 ${KVSTEST} | flux job submit) &&
	flux job attach $id >kvstest.out 2>kvstest.err &&
	grep "t phase" kvstest.out
'
test_expect_success 'job-exec: decrease kill timeout for tests' '
	flux setattr job-exec.kill_timeout 0.1
'
#  Use `!` here instead of test_must_fail because flux-job attach
#   may exit with 143, killed by signal
#
test_expect_success 'job-shell: PMI_Abort works' '
	! flux mini run -N4 -n4 ${PMI_INFO} --abort=1 >abort.log 2>&1 &&
	test_debug "cat abort.log" &&
	grep "job.exception.*MPI_Abort: Test abort error." abort.log
'
test_expect_success 'job-shell: create expected I/O output' '
	${LPTEST} | sed -e "s/^/0: /" >lptest.exp &&
	(for i in $(seq 0 3); do \
		${LPTEST} | sed -e "s/^/$i: /"; \
	done) >lptest4.exp
'
test_expect_success 'job-shell: verify output of 1-task lptest job' '
        id=$(flux jobspec srun -n1 ${LPTEST} | flux job submit) &&
	flux job wait-event $id finish &&
	flux job attach -l $id >lptest.out &&
	test_cmp lptest.exp lptest.out
'
#
# sharness will redirect /dev/null to stdin by default, leading to the
# possibility of seeing an EOF warning on stdin.  We'll check for that
# manually in a few of the tests and filter it out from the stderr
# output.
#

test_expect_success 'job-shell: verify output of 1-task lptest job on stderr' '
        id=$(flux jobspec srun -n1 bash -c "${LPTEST} >&2" \
		| flux job submit) &&
	flux job attach -l $id 2>lptest.err &&
        sed -i -e "/stdin EOF could not be sent/d" lptest.err &&
	test_cmp lptest.exp lptest.err
'
test_expect_success 'job-shell: verify output of 4-task lptest job' '
        id=$(flux jobspec srun -N4 ${LPTEST} | flux job submit) &&
	flux job attach -l $id >lptest4_raw.out &&
	sort -snk1 <lptest4_raw.out >lptest4.out &&
	test_cmp lptest4.exp lptest4.out
'
test_expect_success 'job-shell: verify output of 4-task lptest job on stderr' '
        id=$(flux jobspec srun -N4 bash -c "${LPTEST} 1>&2" \
		| flux job submit) &&
	flux job attach -l $id 2>lptest4_raw.err &&
	sort -snk1 <lptest4_raw.err >lptest4.err &&
        sed -i -e "/stdin EOF could not be sent/d" lptest4.err &&
	test_cmp lptest4.exp lptest4.err
'
test_expect_success LONGTEST 'job-shell: verify 10K line lptest output works' '
	${LPTEST} 79 10000 | sed -e "s/^/0: /" >lptestXXL.exp &&
        id=$(flux jobspec srun -n1 ${LPTEST} 79 10000 | flux job submit) &&
	flux job attach -l $id >lptestXXL.out &&
	test_cmp lptestXXL.exp lptestXXL.out
'
test_expect_success 'job-shell: test shell kill event handling' '
	id=$(flux jobspec srun -N4 sleep 60 | flux job submit) &&
	flux job wait-event $id start &&
	flux job kill $id &&
	flux job wait-event $id finish >kill1.finish.out &&
	grep status=$((15+128<<8)) kill1.finish.out
'
test_expect_success 'job-shell: test shell kill event handling: SIGKILL' '
	id=$(flux jobspec srun -N4 sleep 60 | flux job submit) &&
	flux job wait-event $id start &&
	flux job kill -s SIGKILL $id &&
	flux job wait-event $id finish >kill2.finish.out &&
	grep status=$((9+128<<8)) kill2.finish.out
'
test_expect_success 'job-shell: test shell kill event handling: numeric signal' '
	id=$(flux jobspec srun -N4 sleep 60 | flux job submit) &&
	flux job wait-event $id start &&
	flux job kill -s 2 $id &&
	flux job wait-event $id finish >kill3.finish.out &&
	grep status=$((2+128<<8)) kill3.finish.out
'
test_expect_success 'job-shell: mangled shell kill event logged' '
	id=$(flux jobspec srun -N4 sleep 60 | flux job submit) &&
	flux job wait-event $id start &&
	flux event pub shell-${id}.kill "{}" &&
	flux job kill ${id} &&
	flux job wait-event -vt 1 $id finish >kill4.finish.out &&
	test_debug "cat kill4.finish.out" &&
	test_expect_code 143 flux job attach ${id} >kill4.log 2>&1 &&
	grep "ignoring malformed event" kill4.log
'
test_expect_success 'job-shell: shell kill event: kill(2) failure logged' '
	id=$(flux jobspec srun -N4 sleep 60 | flux job submit) &&
	flux job wait-event $id start &&
	flux event pub shell-${id}.kill "{\"signum\":199}" &&
	flux job kill ${id} &&
	flux job wait-event $id finish >kill5.finish.out &&
	test_debug "cat kill5.finish.out" &&
	test_expect_code 143 flux job attach ${id} >kill5.log 2>&1 &&
	grep "signal 199: Invalid argument" kill5.log &&
	grep status=$((15+128<<8)) kill5.finish.out
'
test_expect_success 'job-shell: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
