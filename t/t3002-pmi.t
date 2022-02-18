#!/bin/sh
#

test_description="Test Flux PMI implementation"

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_CORES_PER_RANK=4
SIZE=$(test_size_large)
test_under_flux ${SIZE} job

kvstest=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest
kvstest2=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest2
pmi_info=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
pmi2_info=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi2_info

test_expect_success 'pmi_info works' '
	flux mini run -n${SIZE} -N${SIZE} ${pmi_info}
'

test_expect_success 'pmi_info --clique shows each node with own clique' '
	flux mini run -opmi.clique=pershell -n${SIZE} -N${SIZE} \
		${pmi_info} --clique >clique.out &&
	count=$(cut -f2 -d: clique.out | sort | uniq | wc -l) &&
	test $count -eq ${SIZE}
'

test_expect_success 'kvstest works' '
	flux mini run -n${SIZE} -N${SIZE} ${kvstest}
'

test_expect_success 'kvstest works with -o pmi.exchange.k=1' '
	flux mini run -n${SIZE} -N${SIZE} -o pmi.exchange.k=1 ${kvstest}
'
test_expect_success 'kvstest works with -o pmi.exchange.k=SIZE' '
	flux mini run -n${SIZE} -N${SIZE} -o pmi.exchange.k=${SIZE} \
		${kvstest} 2>kvstest_k.err &&
	grep "using k=${SIZE}" kvstest_k.err
'
test_expect_success 'kvstest works with -o pmi.exchange.k=SIZE+1' '
	flux mini run -n${SIZE} -N${SIZE} -o pmi.exchange.k=$((${SIZE}+1)) \
		${kvstest} 2>kvstest_kp1.err &&
	grep "using k=${SIZE}" kvstest_kp1.err
'

test_expect_success 'kvstest fails with -o pmi.kvs=unknown' '
	test_must_fail flux mini run -o pmi.kvs=unknown ${kvstest}
'

test_expect_success 'kvstest works with -o pmi.kvs=native' '
	flux mini run -n${SIZE} -N${SIZE} -o pmi.kvs=native ${kvstest}
'

test_expect_success 'kvstest -N8 works' '
	flux mini run -n${SIZE} -N${SIZE} ${kvstest} -N8
'

test_expect_success 'kvstest -N8 works with -o pmi.kvs=native' '
	flux mini run -n${SIZE} -N${SIZE} -o pmi.kvs=native ${kvstest} -N8
'

test_expect_success 'verbose=2 shell option enables PMI server side tracing' '
	flux mini run -n${SIZE} -N${SIZE} -o verbose=2 ${kvstest} 2>trace.out &&
	grep "cmd=finalize_ack" trace.out
'

test_expect_success 'pmi2_info works' '
	flux mini run -n${SIZE} -N${SIZE} ${pmi2_info}
'

# Run on one node only, to avoid the job spanning multiple brokers.
# "Node-scope" PMI2 attributes are really "shell scope", and
# PMI_process_mapping is used by the test to expect which ranks can exchange
# node scope attributes.
test_expect_success 'kvstest2 works' '
	flux mini run -n4 -N1 -overbose=2 ${kvstest2}
'

# Abort test uses ! for expected failure rather than 'test_expect_code'
# because the job exit code is not deterministic.  'test_must_fail' cannot be
# used either because 128 + SIGNUM is not accepted as "failure".

test_expect_success 'PMI application abort is handled properly' '
	! run_timeout 60 flux mini run -overbose=2 \
		${pmi_info} --abort 0
'
test_expect_success 'PMI2 application abort is handled properly' '
	! run_timeout 60 flux mini run -overbose=2 \
		${pmi2_info} --abort 0
'

test_done
