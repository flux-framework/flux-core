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

test_expect_success 'flux run sets PMI_FD' '
	flux run printenv PMI_FD
'
test_expect_success 'flux run sets FLUX_PMI_LIBRARY_PATH' '
	flux run printenv FLUX_PMI_LIBRARY_PATH
'
test_expect_success 'flux run sets LD_LIBRARY_PATH if unset' '
	flux run --env=-LD_LIBRARY_PATH printenv LD_LIBRARY_PATH \
	    >libpath.out &&
	echo $(dirname $(flux config builtin pmi_library_path)) \
	    >libpath.exp &&
	test_cmp libpath.exp libpath.out
'
test_expect_success 'flux run prepends to LD_LIBRARY_PATH if set' '
	flux run --env=LD_LIBRARY_PATH=/a printenv LD_LIBRARY_PATH \
	    >libpath2.out &&
	echo $(dirname $(flux config builtin pmi_library_path)):/a \
	    >libpath2.exp &&
	test_cmp libpath2.exp libpath2.out
'
test_expect_success 'flux run -o pmi=off does not set LD_LIBRARY_PATH' '
	test_must_fail flux run -o pmi=off --env=-LD_LIBRARY_PATH \
	    printenv LD_LIBRARY_PATH
'
test_expect_success 'flux run -o pmi=simple sets PMI_FD' '
	flux run -o pmi=simple printenv PMI_FD
'
test_expect_success 'flux run -o pmi=pmi1 sets PMI_FD' '
	flux run -o pmi=pmi1 printenv PMI_FD
'
test_expect_success 'flux run -o pmi=pmi2 sets PMI_FD' '
	flux run -o pmi=pmi2 printenv PMI_FD
'
test_expect_success 'flux run -o pmi=simple,unknown sets PMI_FD' '
	flux run -o pmi=simple,unknown printenv PMI_FD
'
test_expect_success 'flux run -o pmi=unknown,simple sets PMI_FD' '
	flux run -o pmi=unknown,simple printenv PMI_FD
'
test_expect_success 'flux run -o pmi=unknown does not set PMI_FD' '
	test_must_fail flux run -o pmi=unknown printenv PMI_FD
'
test_expect_success 'flux run -o pmi=off does not set PMI_FD' '
	test_must_fail flux run -o pmi=off printenv PMI_FD
'
test_expect_success 'flux run -o pmi=off does not set FLUX_PMI_LIBRARY_PATH' '
	test_must_fail flux run -o pmi=off printenv FLUX_PMI_LIBRARY_PATH
'
test_expect_success 'flux run -o pmi.badopt fails' '
	test_must_fail flux run -o pmi.badopt true
'
test_expect_success 'flux run -o pmi-simple.exchange.badopt fails' '
	test_must_fail flux run -o pmi-simple.exchange.badopt true
'
test_expect_success 'flux run -o pmi-simple.exchange.k=foo fails' '
	test_must_fail flux run -o pmi-simple.exchange.k=foo true
'
test_expect_success 'flux run -o pmi-simple.nomap=foo fails' '
	test_must_fail flux run -o pmi-simple.nomap=foo true
'

test_expect_success 'pmi_info works' '
	flux run -n${SIZE} -N${SIZE} ${pmi_info}
'

test_expect_success 'pmi_info --clique shows each node with own clique' '
	flux run -n${SIZE} -N${SIZE} \
		${pmi_info} --clique >clique.out &&
	count=$(cut -f2 -d: clique.out | sort | uniq | wc -l) &&
	test $count -eq ${SIZE}
'
test_expect_success 'pmi_info --clique none shows each task in its own clique' '
	flux run -opmi-simple.nomap -n${SIZE} -N${SIZE} \
		${pmi_info} --clique >clique.none.out &&
	count=$(cut -f2 -d: clique.none.out | sort | uniq | wc -l) &&
	test $count -eq ${SIZE}
'
test_expect_success 'kvstest works' '
	flux run -n${SIZE} -N${SIZE} ${kvstest}
'

test_expect_success 'kvstest works with -o pmi-simple.exchange.k=1' '
	flux run -n${SIZE} -N${SIZE} -o pmi-simple.exchange.k=1 ${kvstest}
'
test_expect_success 'kvstest works with -o pmi-simple.exchange.k=SIZE' '
	flux run -n${SIZE} -N${SIZE} -o pmi-simple.exchange.k=${SIZE} \
		${kvstest} 2>kvstest_k.err &&
	grep "using k=${SIZE}" kvstest_k.err
'
test_expect_success 'kvstest works with -o pmi-simple.exchange.k=SIZE+1' '
	flux run -n${SIZE} -N${SIZE} -o pmi-simple.exchange.k=$((${SIZE}+1)) \
		${kvstest} 2>kvstest_kp1.err &&
	grep "using k=${SIZE}" kvstest_kp1.err
'

test_expect_success 'kvstest fails with -o pmi-simple.kvs=unknown' '
	test_must_fail flux run -o pmi-simple.kvs=unknown ${kvstest}
'

test_expect_success 'kvstest -N8 works' '
	flux run -n${SIZE} -N${SIZE} ${kvstest} -N8
'

test_expect_success 'verbose=2 shell option enables PMI server side tracing' '
	flux run -n${SIZE} -N${SIZE} -o verbose=2 ${kvstest} 2>trace.out &&
	grep "cmd=finalize_ack" trace.out
'

test_expect_success 'pmi2_info works' '
	flux run -n${SIZE} -N${SIZE} ${pmi2_info}
'

# Run on one node only, to avoid the job spanning multiple brokers.
# "Node-scope" PMI2 attributes are really "shell scope", and
# PMI_process_mapping is used by the test to expect which ranks can exchange
# node scope attributes.
test_expect_success 'kvstest2 works' '
	flux run -n4 -N1 -overbose=2 ${kvstest2}
'

# Abort test uses ! for expected failure rather than 'test_expect_code'
# because the job exit code is not deterministic.  'test_must_fail' cannot be
# used either because 128 + SIGNUM is not accepted as "failure".

test_expect_success 'PMI application abort is handled properly' '
	! run_timeout 60 flux run -overbose=2 \
		${pmi_info} --abort 0
'
test_expect_success 'PMI2 application abort is handled properly' '
	! run_timeout 60 flux run -overbose=2 \
		${pmi2_info} --abort 0
'

# Ensure that pmi_info can get clique ranks with a large enough
# number of tasks per node that PMI_process_mapping likely
# overflowed the max value length for the PMI-1 KVS. This ensures
# that the PMI client picked up flux.taskmap instead:

test_expect_success 'PMI1 can calculate clique ranks with 128 tasks per node' '
	flux run -t 1m -N2 --taskmap=cyclic --tasks-per-node=128 \
		${pmi_info} --clique >tpn.128.out &&
	test_debug "cat tpn.128.out" &&
	grep "0: clique=0,2,4,6,8,10,12,14,16,18,20" tpn.128.out
'

test_expect_success 'flux-pmi barrier works' '
	flux run --label-io -n2 \
	    flux pmi barrier --test-timing
'
test_expect_success 'flux-pmi barrier --test-count works' '
	flux run --label-io -n2 \
	    flux pmi barrier --test-timing --test-count=2
'
test_expect_success 'flux-pmi barrier --test-abort works' '
	test_expect_code 143 flux run --label-io -n2 \
	    flux pmi barrier --test-timing --test-abort=1 2>barrier_abort.err &&
	grep -i abort barrier_abort.err
'
test_expect_success 'flux-pmi exchange works' '
	flux run --label-io -n2 \
	    flux pmi exchange
'
test_expect_success 'flux-pmi exchange --count works' '
	flux run --label-io -n2 \
	    flux pmi exchange --count=2
'
test_expect_success 'flux-pmi get PMI_process_mapping works' '
	flux run --label-io -n2 \
	    flux pmi get PMI_process_mapping
'
test_expect_success 'flux-pmi get --ranks=1 flux.taskmap works' '
	flux run --label-io -n2 \
	    flux pmi get --ranks=1 flux.taskmap
'
test_expect_success 'flux-pmi get --ranks=all flux.instance-level works' '
	flux run --label-io -n2 \
	    flux pmi get --ranks=all flux.instance-level
'
test_expect_success 'flux-pmi get works with multiple keys' '
	flux run --label-io -n2 \
	    flux pmi get flux.instance-level PMI_process_mapping
'
test_expect_success 'flux-pmi works outside of job' '
	flux pmi -v --libpmi-noflux barrier --test-timing
'
test_expect_success 'flux-pmi fails with bad subcommand' '
	test_must_fail flux run flux pmi notacmd
'
test_expect_success 'flux-pmi get fails with bad option' '
	test_must_fail flux run flux pmi get --badopt foo
'
test_expect_success 'flux-pmi get fails with bad ranks option' '
	test_must_fail flux run flux pmi get --ranks=1.2 flux.taskmap
'
test_expect_success 'flux-pmi fails with bad ranks option' '
	test_must_fail flux run \
	    flux pmi --method=badmethod barrier
'
# method=simple (covered above also)
test_expect_success 'flux-pmi --method=simple fails outside of job' '
	test_must_fail flux pmi --method=simple barrier
'
test_expect_success 'flux-pmi -v --method=simple works within job' '
	flux run --label-io -n2 \
	    flux pmi -v --method=simple barrier --test-timing
'
test_expect_success 'flux-pmi -opmi=off --method=simple fails' '
	test_must_fail flux run -o pmi=off \
	    flux pmi --method=simple barrier
'
# method=libpmi
test_expect_success 'get pmi library path' '
	flux config builtin pmi_library_path >libpmi
'
test_expect_success 'flux-pmi --method=libpmi:/bad/path fails' '
	test_must_fail flux run \
	    flux pmi --method=libpmi:/bad/path barrier
'
test_expect_success 'flux-pmi --method=libpmi barrier works w/ flux libpmi.so' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi:$(cat libpmi) barrier --test-timing"
'
test_expect_success 'flux-pmi --method=libpmi barrier abort works w/ flux libpmi.so' '
	test_expect_code 143 flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi:$(cat libpmi) barrier --test-abort 1"
'
test_expect_success 'flux-pmi --method=libpmi exchange works w/ flux libpmi.so' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi:$(cat libpmi) exchange"
'
test_expect_success 'flux-pmi --method=libpmi get works w/ flux libpmi.so' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi:$(cat libpmi) get flux.taskmap"
'
test_expect_success 'flux-pmi --libpmi-noflux fails w/ flux libpmi.so' '
	test_must_fail flux run bash -c "\
	    flux pmi --method=libpmi:$(cat libpmi) --libpmi-noflux barrier"
'
test_expect_success 'flux broker refuses the Flux libpmi.so and goes single' '
	test_when_finished "cat debug.err" &&
	FLUX_PMI_DEBUG=1 FLUX_PMI_CLIENT_METHODS="libpmi single" \
	    LD_LIBRARY_PATH=$(dirname $(cat libpmi)):${LD_LIBRARY_PATH} \
            flux start true 2>debug.err &&
	grep single debug.err
'
# method=libpmi2
test_expect_success 'get pmi2 library path' '
	flux config builtin pmi_library_path | sed s/libpmi/libpmi2/ >libpmi2
'
test_expect_success 'flux-pmi --method=libpmi2:/bad/path fails' '
	test_must_fail flux run \
	    flux pmi --method=libpmi2:/bad/path barrier
'
test_expect_success 'flux-pmi --method=libpmi2 barrier works w/ flux pmi lib' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi2:$(cat libpmi2) barrier"
'
test_expect_success 'flux-pmi --method=libpmi2 barrier works w/ flux pmi lib' '
	test_expect_code 143 flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi2:$(cat libpmi2) barrier --test-abort 1"
'
test_expect_success 'flux-pmi --method=libpmi2 exchange works w/ flux pmi lib' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi2:$(cat libpmi2) exchange"
'
test_expect_success 'flux-pmi --method=libpmi2 exchange works w/ cray quirks' '
	flux run -n2 bash -c "\
	    flux pmi -v --libpmi2-cray --method=libpmi2:$(cat libpmi2) exchange"
'
test_expect_success 'flux-pmi --method=libpmi2 get works w/ flux pmi lib' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi2:$(cat libpmi2) get flux.taskmap"
'
test_expect_success 'flux-pmi --method=libpmi2 get fails w/ cray quirks' '
	test_must_fail flux run -n2 bash -c "\
	    flux pmi -v --libpmi2-cray --method=libpmi2:$(cat libpmi2) \
	        get flux.taskmap"
'
test_expect_success 'PMI_process_mapping special case works' '
	flux run -n2 bash -c "\
	    flux pmi -v --method=libpmi2:$(cat libpmi2) get PMI_process_mapping"
'
test_expect_success 'flux-pmi --libpmi-noflux fails w/ flux libpmi2.so' '
	test_must_fail flux run bash -c "\
	    flux pmi --method=libpmi2:$(cat libpmi2) --libpmi-noflux barrier"
'
test_expect_success 'flux broker refuses the Flux pmi lib and goes single' '
	test_when_finished "cat debug.err" &&
	FLUX_PMI_DEBUG=1 FLUX_PMI_CLIENT_METHODS="libpmi2 single" \
	    LD_LIBRARY_PATH=$(dirname $(cat libpmi2)):${LD_LIBRARY_PATH} \
            flux start true 2>debug.err &&
	grep single debug.err
'
# method=single
test_expect_success 'flux-pmi --method=single barrier works' '
	flux pmi -v --method=single barrier
'
test_expect_success 'flux-pmi --method=single exchange works' '
	flux pmi -v --method=single exchange
'
test_expect_success 'flux-pmi --method=single get notakey fails' '
	test_must_fail flux pmi --method=single get notakey
'
test_expect_success 'flux-pmi -opmi=off --method=single works' '
	flux run -o pmi=off \
	    flux pmi --method=single barrier
'
test_done
