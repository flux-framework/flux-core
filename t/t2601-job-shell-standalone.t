#!/bin/sh
#
test_description='Test flux-shell in --standalone mode'

. `dirname $0`/sharness.sh

jq=$(which jq 2>/dev/null)
if test -z "$jq" ; then
    skip_all 'no jq found, skipping all tests'
    test_done
fi

#  Run flux-shell under flux command to get correct paths
FLUX_SHELL="flux ${FLUX_BUILD_DIR}/src/shell/flux-shell"

PMI_INFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
KVSTEST=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest

unset FLUX_URI

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess

test_expect_success 'flux-shell: generate 1-task jobspec and matching R' '
	flux jobspec srun -N1 -n1 echo Hi >j1 &&
	cat >R1 <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0" }, "rank": "0" }
        ]}}
	EOT
'
test_expect_success 'flux-shell: run 1-task echo job' '
	${FLUX_SHELL} -v -s -r 0 -j j1 -R R1 0 >echo.out 2>echo.err &&
	grep Hi echo.out
'
test_expect_success 'flux-shell: -v output includes expected task count' '
	grep task_count=1 echo.err
'
test_expect_success 'flux-shell: missing JOBID fails with Usage message' '
	test_must_fail ${FLUX_SHELL} -s -r 0 -j j1 -R R1 2>nojobid.err &&
	grep Usage nojobid.err
'
test_expect_success 'flux-shell: missing -r fails with standalone message' '
	test_must_fail ${FLUX_SHELL} -s -j j1 -R R1 0 2>no_r.err &&
	grep standalone no_r.err
'
test_expect_success 'flux-shell: missing -R fails with standalone message' '
	test_must_fail ${FLUX_SHELL} -s -r 0 -j j1 0 2>no_R.err &&
	grep standalone no_R.err
'
test_expect_success 'flux-shell: missing -j fails with standalone message' '
	test_must_fail ${FLUX_SHELL} -s -r 0 -R R1 0 2>no_j.err &&
	grep standalone no_j.err
'
test_expect_success 'flux-shell: nonexistent jobspec file fails' '
	! ${FLUX_SHELL} -v -s -r 0 -j /noexist -R R1 0 \
		>noexist.out 2>noexist.err &&
	grep "error opening" noexist.err
'
test_expect_success 'flux-shell: malformed jobid fails' '
	! ${FLUX_SHELL} -v -s -r 0 -j j1 -R R1 BADID \
		>badid.out 2>badid.err &&
	grep "jobid" badid.err
'
test_expect_success 'flux-shell: out of range jobid fails' '
	! ${FLUX_SHELL} -v -s -r 0 -j j1 -R R1 18446744073709551616 \
		>badid2.out 2>badid2.err &&
	grep "jobid" badid2.err
'
test_expect_success 'flux-shell: out of range broker rank fails' '
	test_must_fail ${FLUX_SHELL} -s -r 4294967296 -j j1 -R R1 0 2>er.err &&
	grep -i option er.err
'
test_expect_success 'flux-shell: wrong range broker rank fails' '
	test_must_fail ${FLUX_SHELL} -s -r 8 -j j1 -R R1 0 2>wr.err &&
	grep -i fetching wr.err
'
test_expect_success 'flux-shell: unknown argument fails' '
	test_must_fail ${FLUX_SHELL} --FOO -s -r 0 -j j1 -R R1 0 2>alien.err &&
	grep -i unrecognized alien.err
'
test_expect_success 'flux-shell: generate 2-task jobspec and matching R' '
	flux jobspec srun -N1 -n2 printenv >j2 &&
	cat >R2 <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0-1" }, "rank": "0" }
        ]}}
	EOT
'
test_expect_success 'flux-shell: run 2-task printenv job' '
	${FLUX_SHELL} -v -s -r 0 -j j2 -R R2 42 \
		>printenv.out 2>printenv.err
'
test_expect_success 'flux-shell: 0: completed with no error' '
	grep "task 0 complete status=0" printenv.err
'
test_expect_success 'flux-shell: 1: completed with no error' '
	grep "task 1 complete status=0" printenv.err
'
test_expect_success 'flux-shell: 0: FLUX_TASK_LOCAL_ID, TASK_RANK set' '
	grep FLUX_TASK_LOCAL_ID=0 printenv.out &&
	grep FLUX_TASK_RANK=0 printenv.out
'
test_expect_success 'flux-shell: 1: FLUX_TASK_LOCAL_ID, TASK_RANK set' '
	grep FLUX_TASK_LOCAL_ID=1 printenv.out &&
	grep FLUX_TASK_RANK=1 printenv.out
'
test_expect_success 'flux-shell: FLUX_JOB_SIZE, JOB_NNODES, JOB_ID set' '
	grep FLUX_JOB_SIZE=2 printenv.out &&
	grep FLUX_JOB_NNODES=1 printenv.out &&
	grep FLUX_JOB_ID=42 printenv.out
'
test_expect_success 'flux-shell: FLUX_URI is not set by shell' '
	test_must_fail grep FLUX_URI printenv.out
'
test_expect_success 'flux-shell: 0: PMI_RANK set' '
	grep PMI_RANK=0 printenv.out
'
test_expect_success 'flux-shell: 1: PMI_RANK set' '
	grep PMI_RANK=1 printenv.out
'
test_expect_success 'flux-shell: PMI_SIZE, PMI_FD set' '
	grep PMI_SIZE=2 printenv.out &&
	grep PMI_FD= printenv.out
'
test_expect_success 'flux-shell: generate 8-task bash exit rank job' '
	flux jobspec srun -N1 -n8 bash -c "exit \$FLUX_TASK_RANK" >j8 &&
	cat >R8 <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0-7" }, "rank": "0" }
        ]}}
	EOT
'
test_expect_success 'flux-shell: environ in jobspec is set for task' '
	flux jobspec srun --export ENVTEST=foo printenv >je &&
	${FLUX_SHELL} -v -s -r 0 -j je -R R1 42 \
		>printenv2.out 2>printenv2.err &&
	grep ENVTEST=foo printenv2.out
'
test_expect_success 'flux-shell: shell PMI works' '
	flux jobspec srun -N1 -n8 ${PMI_INFO} >j8pmi &&
	${FLUX_SHELL} -v -s -r 0 -j j8pmi -R R8 51 \
		>pmi_info.out 2>pmi_info.err
'
test_expect_success 'flux-shell: shell PMI exports clique info' '
	flux jobspec srun -N1 -n8 ${PMI_INFO} -c >j8pmi_clique &&
	${FLUX_SHELL} -v -s -r 0 -j j8pmi_clique -R R8 51 \
		>pmi_clique.out 2>pmi_clique.err &&
	COUNT=$(grep "clique=0,1,2,3,4,5,6,7" pmi_clique.out | wc -l) &&
	test ${COUNT} -eq 8
'
test_expect_success 'flux-shell: shell PMI KVS works' '
	flux jobspec srun -N1 -n8 ${KVSTEST} >j8kvs &&
	${FLUX_SHELL} -v -s -r 0 -j j8kvs -R R8 52 \
		>kvstest.out 2>kvstest.err
'
test_expect_success 'flux-shell: shell can launch flux' '
	flux jobspec srun -N1 -n8 flux start flux comms info >j8flux &&
	${FLUX_SHELL} -v -s -r 0 -j j8flux -R R8 39 \
		>flux.out 2>flux.err &&
	grep size=8 flux.out
'

test_expect_success 'flux-shell: shell exits with highest task exit value' '
	test_expect_code 7 ${FLUX_SHELL} -v -s -r 0 -j j8 -R R8 69 \
		>exit.out 2>exit.err
'

test_expect_success 'flux-shell: shell forwards signals to tasks' '
	flux jobspec srun -n1 bash -c "kill \$PPID; sleep 10" > j9 &&
	test_expect_code  $((128+15)) \
		${FLUX_SHELL} -v -s -r 0 -j j9 -R R8 69 \
			>sigterm.out 2>sigterm.err &&
	grep "forwarding signal 15" sigterm.err
'

test_expect_success 'flux-shell: generate 1-task echo jobspecs and matching R' '
	flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo > j1echostdout &&
	flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar > j1echostderr &&
	flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz > j1echoboth &&
	cat >R1echo <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0" }, "rank": "0" }
        ]}}
	EOT
'

test_expect_success 'flux-shell: generate 2-task echo jobspecs and matching R' '
	flux jobspec srun -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo > j2echostdout &&
	flux jobspec srun -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar > j2echostderr &&
	flux jobspec srun -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz > j2echoboth &&
	cat >R2echo <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0-1" }, "rank": "0" }
        ]}}
	EOT
'

test_expect_success 'flux-shell: run 1-task echo job (stdout file)' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out100\"" \
            > j1echostdout-100 &&
        ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-100 -R R1echo 100 &&
	grep stdout:foo out100
'

test_expect_success 'flux-shell: run 1-task echo job (stderr file)' '
        cat j1echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err101\"" \
            > j1echostderr-101 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostderr-101 -R R1echo 101 &&
	grep stderr:bar err101
'

test_expect_success 'flux-shell: run 1-task echo job (stdout file/stderr file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out102\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err102\"" \
            > j1echoboth-102 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-102 -R R1echo 102 &&
	grep stdout:baz out102 &&
	grep stderr:baz err102
'

test_expect_success 'flux-shell: run 1-task echo job (stdout file/stderr term)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out103\"" \
            > j1echoboth-103 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-103 -R R1echo 2> err103 103 &&
	grep stdout:baz out103 &&
	grep stderr:baz err103
'

test_expect_success 'flux-shell: run 1-task echo job (stdout term/stderr file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err104\"" \
            > j1echoboth-104 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-104 -R R1echo > out104 104 &&
	grep stdout:baz out104 &&
	grep stderr:baz err104
'

test_expect_success 'flux-shell: run 2-task echo job (stdout file)' '
        cat j2echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out105\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            > j2echostdout-105 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostdout-105 -R R2echo 105 &&
	grep "0: stdout:foo" out105 &&
	grep "1: stdout:foo" out105
'

test_expect_success 'flux-shell: run 2-task echo job (stderr file)' '
        cat j2echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err106\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echostderr-106 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostderr-106 -R R2echo 106 &&
	grep "0: stderr:bar" err106 &&
	grep "1: stderr:bar" err106
'

test_expect_success 'flux-shell: run 2-task echo job (stdout file/stderr file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out107\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err107\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echoboth-107 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-107 -R R2echo 107 &&
	grep "0: stdout:baz" out107 &&
	grep "1: stdout:baz" out107 &&
	grep "0: stderr:baz" err107 &&
	grep "1: stderr:baz" err107
'

test_expect_success 'flux-shell: run 2-task echo job (stdout file/stderr term)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out108\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            > j2echoboth-108 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-108 -R R2echo 2> err108 108 &&
	grep "0: stdout:baz" out108 &&
	grep "1: stdout:baz" out108 &&
	grep "0: stderr:baz" err108 &&
	grep "1: stderr:baz" err108
'

test_expect_success 'flux-shell: run 2-task echo job (stdout term/stderr file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err109\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echoboth-109 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-109 -R R2echo > out109 109 &&
	grep "0: stdout:baz" out109 &&
	grep "1: stdout:baz" out109 &&
	grep "0: stderr:baz" err109 &&
	grep "1: stderr:baz" err109
'

test_expect_success 'flux-shell: run 1-task echo job (per-task stdout)' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out110\"" \
            > j1echostdout-110 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostdout-110 -R R1echo 110 &&
	grep stdout:foo out110-0
'

test_expect_success 'flux-shell: run 1-task echo job (per-task stderr)' '
        cat j1echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err111\"" \
            > j1echostderr-111 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostderr-111 -R R1echo 111 &&
	grep stderr:bar err111-0
'

test_expect_success 'flux-shell: run 1-task echo job (per-task stdout & stderr)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out112\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err112\"" \
            > j1echoboth-112 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-112 -R R1echo 112 &&
	grep stdout:baz out112-0 &&
	grep stderr:baz err112-0
'

test_expect_success 'flux-shell: run 1-task echo job (per-task stdout/stderr term)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out113\"" \
            > j1echoboth-113 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-113 -R R1echo 2> err113 113 &&
	grep stdout:baz out113-0 &&
	grep stderr:baz err113
'

test_expect_success 'flux-shell: run 1-task echo job (stdout term/per-task stderr)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err114\"" \
            > j1echoboth-114 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-114 -R R1echo > out114 114 &&
	grep stdout:baz out114 &&
	grep stderr:baz err114-0
'

test_expect_success 'flux-shell: run 2-task echo job (per-task stdout)' '
        cat j2echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out115\"" \
            > j2echostdout-115 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostdout-115 -R R2echo 115 &&
	grep "stdout:foo" out115-0 &&
	grep "stdout:foo" out115-1
'

test_expect_success 'flux-shell: run 2-task echo job (per-task stderr)' '
        cat j2echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err116\"" \
            > j2echostderr-116 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostderr-116 -R R2echo 116 &&
	grep "stderr:bar" err116-0 &&
	grep "stderr:bar" err116-1
'

test_expect_success 'flux-shell: run 2-task echo job (per-task stdout & stderr)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out117\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err117\"" \
            > j2echoboth-117 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-117 -R R2echo 117 &&
	grep "stdout:baz" out117-0 &&
	grep "stdout:baz" out117-1 &&
	grep "stderr:baz" err117-0 &&
	grep "stderr:baz" err117-1
'

test_expect_success 'flux-shell: run 2-task echo job (per-task stdout/stderr term)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out118\"" \
            > j2echoboth-118 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-118 -R R2echo 2> err118 118 &&
	grep "stdout:baz" out118-0 &&
	grep "stdout:baz" out118-1 &&
	grep "0: stderr:baz" err118 &&
	grep "1: stderr:baz" err118
'

test_expect_success 'flux-shell: run 2-task echo job (stdout term/per-task stderr)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err119\"" \
            > j2echoboth-119 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-119 -R R2echo > out119 119 &&
	grep "0: stdout:baz" out119 &&
	grep "1: stdout:baz" out119 &&
	grep "stderr:baz" err119-0 &&
	grep "stderr:baz" err119-1
'

test_expect_success 'flux-shell: run 2-task echo job (per-task stdout/stderr file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out120\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err120\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echoboth-120 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-120 -R R2echo 120 &&
	grep "stdout:baz" out120-0 &&
	grep "stdout:baz" out120-1 &&
	grep "0: stderr:baz" err120 &&
	grep "1: stderr:baz" err120
'

test_expect_success 'flux-shell: run 2-task echo job (stdout file/per-task stderr)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out121\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err121\"" \
            > j2echoboth-121 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-121 -R R2echo 121 &&
	grep "0: stdout:baz" out121 &&
	grep "1: stdout:baz" out121 &&
	grep "stderr:baz" err121-0 &&
	grep "stderr:baz" err121-1
'

test_expect_success 'flux-shell: error on bad output type' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"foobar\"" \
            > j1echostdout-122 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-122 -R R1echo 122
'

test_expect_success 'flux-shell: error on no path with file output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            > j1echostdout-123 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-123 -R R1echo 123
'

test_expect_success 'flux-shell: error on no path with per-task output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            > j1echostdout-124 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-124 -R R1echo 124
'

test_expect_success 'flux-shell: error invalid path to file output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"/foo/bar/baz\"" \
            > j1echostdout-125 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-125 -R R1echo 125
'

test_expect_success 'flux-shell: error invalid path to per-task output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"per-task\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"/foo/bar/baz\"" \
            > j1echostdout-126 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-126 -R R1echo 126
'

test_done
