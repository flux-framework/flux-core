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
grouptest=${FLUX_BUILD_DIR}/src/common/libpmi/test_grouptest
threadtest=${FLUX_BUILD_DIR}/src/common/libpmi/test_threadtest

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
		-o verbose ${kvstest} 2>kvstest_k.err &&
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
test_expect_success 'PMI server offers pmi_subversion=2 by default' '
	flux run -n1 -o verbose=2 flux pmi barrier 2>ver.err &&
	grep "response_to_init rc=0 pmi_version=1 pmi_subversion=2" ver.err
'
test_expect_success 'pmi-simple.version=1.1 offers pmi_subversion=1' '
	flux run -n1 -o verbose=2 -o pmi-simple.version=1.1 \
	    flux pmi barrier 2>ver11.err &&
	grep "response_to_init rc=0 pmi_version=1 pmi_subversion=1" ver11.err
'
test_expect_success 'pmi-simple.version=1.2 offers pmi_subversion=2' '
	flux run -n1 -o verbose=2 -o pmi-simple.version=1.2 \
	    flux pmi barrier 2>ver12.err &&
	grep "response_to_init rc=0 pmi_version=1 pmi_subversion=2" ver12.err
'
test_expect_success 'pmi-simple.version accepts a string' '
	flux run -n1 -o verbose=2 -o pmi-simple.version=\"1.1\" \
	    flux pmi barrier 2>verstr.err &&
	grep "response_to_init rc=0 pmi_version=1 pmi_subversion=1" verstr.err
'
test_expect_success 'pmi-simple.version=1.0 is rejected' '
	test_must_fail flux run -n1 -o pmi-simple.version=1.0 true 2>ver10.err &&
	grep "version must be 1.1 or 1.2" ver10.err
'
test_expect_success 'pmi-simple.version=2.0 is rejected' '
	test_must_fail flux run -n1 -o pmi-simple.version=2.0 true 2>ver20.err &&
	grep "version must be 1.1 or 1.2" ver20.err
'
# An explicit group string grows with the job size, so unlike every other
# PMI-1 request it may exceed the KVS-derived protocol line limit.  Drive
# the wire protocol directly since the libpmi client here has no group
# barrier.  With 400 ranks the request line is about 1.5K.
test_expect_success 'PMI-1.2 group barrier with a long rank list works' '
	cat >longgroup.sh <<-EOT &&
	#!/bin/bash
	group=\$(seq -s, 0 \$((FLUX_JOB_SIZE-1)))
	echo "cmd=init pmi_version=1 pmi_subversion=2" >&\$PMI_FD
	read -u \$PMI_FD line
	echo "cmd=barrier_in group=\${group}:7" >&\$PMI_FD
	read -u \$PMI_FD line
	echo "\$line"
	echo "cmd=finalize" >&\$PMI_FD
	read -u \$PMI_FD line
	EOT
	chmod +x longgroup.sh &&
	flux run -t 2m -N1 --tasks-per-node=400 ./longgroup.sh >longgroup.out &&
	test $(grep -c "cmd=barrier_out tag=7 rc=0" longgroup.out) -eq 400
'
# Against a 1.1 server, group barriers fail early in the client without
# touching the wire, the PMI_GROUP_SELF probe fails too, and everything
# else still works.
test_expect_success 'flux-pmi barrier --group=world fails on a 1.1 server' '
	test_must_fail flux run -n2 -N2 -o pmi-simple.version=1.1 \
	    flux pmi barrier --group=world 2>grp11.err &&
	grep "does not support PMI-1.2" grp11.err
'
test_expect_success 'PMI_Barrier_group self probe fails on a 1.1 server' '
	test_must_fail flux run -n2 -N2 -o pmi-simple.version=1.1 \
	    flux pmi --method=libpmi barrier --group=self 2>self11.err &&
	grep "operation failed" self11.err
'
test_expect_success 'kvstest works on a 1.1 server' '
	flux run -n8 -N4 -o pmi-simple.version=1.1 ${kvstest}
'
test_expect_success 'legacy barrier via libpmi works on a 1.1 server' '
	flux run -n8 -N4 -o pmi-simple.version=1.1 ${threadtest} 1 3
'
# PMI-1.2 group barriers.  Use 4 shells x 2 tasks so groups can span a
# subset of shells; non-member ranks idle while members barrier.
test_expect_success 'flux-pmi barrier --group=world works' '
	flux run -n8 -N4 flux pmi barrier --group=world --tag=hello
'
test_expect_success 'flux-pmi barrier --group=node works' '
	flux run -n8 -N4 flux pmi barrier --group=node
'
# A node barrier spans only the shell's own ranks: ranks on shell 0 enter
# one while the other shells' ranks never enter any barrier.
test_expect_success 'flux-pmi barrier --group=node spans one shell' '
	cat >nodescope.sh <<-EOT &&
	#!/bin/sh
	if test \$FLUX_TASK_RANK -lt 2; then
	    flux pmi barrier --group=node
	else
	    sleep 1
	fi
	EOT
	chmod +x nodescope.sh &&
	run_timeout 30 flux run -n8 -N4 ./nodescope.sh >nodescope.out &&
	grep "group barrier (node)" nodescope.out
'
test_expect_success 'flux-pmi barrier --group=self works' '
	flux run -n8 -N4 flux pmi barrier --group=self
'
test_expect_success 'flux-pmi barrier --group spanning 2 of 4 shells works' '
	flux run -n8 -N4 flux pmi barrier --group=0,1,4,5 >sub1.out &&
	grep "group barrier (0,1,4,5)" sub1.out
'
test_expect_success 'flux-pmi barrier --group excluding rank 0 works' '
	flux run -n8 -N4 flux pmi barrier --group=2,3,6,7 >sub2.out &&
	grep "group barrier (2,3,6,7)" sub2.out
'
test_expect_success 'flux-pmi barrier --group with one rank per shell works' '
	flux run -n8 -N4 flux pmi barrier --group=1,3,5,7 --test-count=4
'
test_expect_success 'flux-pmi barrier --group=IDSET with a single rank works' '
	flux run -n8 -N4 flux pmi barrier --group=5
'
test_expect_success 'flux-pmi barrier --group echoes the hashed string tag' '
	flux run -n8 -N4 -o verbose=2 \
	    flux pmi barrier --group=6,7 --tag=hello 2>tag.err &&
	grep "cmd=barrier_in group=6,7:1335831723" tag.err &&
	grep "cmd=barrier_out tag=1335831723" tag.err
'
test_expect_success 'flux-pmi barrier --group with no tag uses tag 0' '
	flux run -n8 -N4 -o verbose=2 \
	    flux pmi barrier --group=6,7 2>tag0.err &&
	grep "cmd=barrier_in group=6,7:0" tag0.err
'
test_expect_success 'flux-pmi barrier --group works via dlopened libpmi.so' '
	flux run -n8 -N4 flux pmi --method=libpmi barrier --group=2,3,6,7
'
test_expect_success 'flux-pmi barrier --group fails via libpmi2' '
	test_must_fail flux run -n2 -N2 \
	    flux pmi --method=libpmi2 barrier --group=world 2>pmi2.err &&
	grep "not implemented" pmi2.err
'
# Two disjoint groups each barrier concurrently in the same job.
test_expect_success 'two concurrent disjoint group barriers complete' '
	cat >twogroups.sh <<-EOT &&
	#!/bin/sh
	rank=\$FLUX_TASK_RANK
	if test \$((rank % 2)) -eq 0; then
	    flux pmi barrier --group=0,2,4,6 --tag=even --test-count=3
	else
	    flux pmi barrier --group=1,3,5,7 --tag=odd --test-count=3
	fi
	EOT
	chmod +x twogroups.sh &&
	flux run -n8 -N4 ./twogroups.sh >twogroups.out &&
	test $(grep -c "group barrier (0,2,4,6)" twogroups.out) -eq 3 &&
	test $(grep -c "group barrier (1,3,5,7)" twogroups.out) -eq 3
'
# A member beyond the job size cannot be mapped to a shell, so the shell
# fails the barrier and the members get PMI_FAIL.
test_expect_success 'flux-pmi barrier --group with an out-of-range rank fails' '
	test_must_fail flux run -n4 -N2 \
	    flux pmi barrier --group=0,99 2>range.err &&
	grep "cannot map members to shells" range.err &&
	grep "operation failed" range.err
'
# An abort while other ranks are blocked in a group barrier still tears
# the job down with the abort exit status.  The aborting rank is a group
# member that never enters, so the others cannot complete the barrier.
test_expect_success 'PMI abort while ranks wait in a group barrier works' '
	cat >groupabort.sh <<-EOT &&
	#!/bin/sh
	if test \$FLUX_TASK_RANK -eq 1; then
	    exec flux pmi barrier --test-abort=1
	fi
	exec flux pmi barrier --group=0-3
	EOT
	chmod +x groupabort.sh &&
	test_expect_code 143 flux run -n4 -N2 ./groupabort.sh 2>groupabort.err &&
	grep -i abort groupabort.err
'
# KVS visibility: a group barrier makes member puts visible to members
test_expect_success 'flux-pmi exchange --group=world works' '
	flux run -n8 -N4 flux pmi exchange --group=world
'
test_expect_success 'flux-pmi exchange --group spanning 2 of 4 shells works' '
	flux run -n8 -N4 flux pmi exchange --group=2,3,6,7 >ex1.out &&
	grep "exchange on 4 tasks" ex1.out
'
test_expect_success 'flux-pmi exchange --group=self works' '
	flux run -n8 -N4 flux pmi exchange --group=self --count=2
'
test_expect_success 'flux-pmi exchange --group=node is rejected' '
	test_must_fail flux run -n2 -N1 flux pmi exchange --group=node
'
# grouptest checks KVS visibility through libpmi: group members see each
# other's puts after a group barrier, and a later whole-job barrier still
# delivers every rank's puts (including the subset's) to every rank.
test_expect_success 'grouptest: group on 2 of 4 shells' '
	flux run -n8 -N4 ${grouptest} 0-3
'
test_expect_success 'grouptest: group on half of each shell' '
	flux run -n8 -N4 ${grouptest} 1,3,5,7
'
test_expect_success 'grouptest: group excluding rank 0' '
	flux run -n8 -N4 ${grouptest} 2,3,6,7
'
test_expect_success 'grouptest: single rank group' '
	flux run -n8 -N4 ${grouptest} 5
'
test_expect_success 'grouptest: group of all ranks' '
	flux run -n8 -N4 ${grouptest} 0-7
'
test_expect_success 'grouptest: node group on 4 shells' '
	flux run -n8 -N4 ${grouptest} node
'
test_expect_success 'grouptest: node group on 1 shell' '
	flux run -n4 -N1 ${grouptest} node
'
# MPICH may list a group in descending rank order (flux-core issue #7888).
test_expect_success 'grouptest: descending rank list on 2 of 4 shells' '
	flux run -n8 -N4 ${grouptest} -r 0-3
'
test_expect_success 'grouptest: descending rank list on half of each shell' '
	flux run -n8 -N4 ${grouptest} -r 1,3,5,7
'
# Overlapping groups in sequence: each group spans two shells and shares
# one with the next.  A subset exchange must not retire the keys it
# exchanged, since the shared shell's keys are needed again by the next
# group, and the final whole-job barrier still delivers every key.
test_expect_success 'grouptest: overlapping groups in sequence' '
	flux run -n8 -N4 ${grouptest} 0-3 2-5 4-7
'
test_expect_success 'grouptest: node group between explicit groups' '
	flux run -n8 -N4 ${grouptest} 0-3 node 4-7
'
# threadtest runs barriers concurrently from several threads of each
# rank over one PMI_FD, with a KVS exchange around each.  In world mode
# the barriers are tagged and legacy whole-job barriers; in split mode
# each thread barriers over a different explicit subset; in node mode
# each thread barriers over the clique with its own tag.
test_expect_success 'threadtest: 4 threads x 10 rounds on 4 shells' '
	run_timeout 60 flux run -n8 -N4 ${threadtest} 4 10
'
test_expect_success 'threadtest: 8 threads x 5 rounds on 1 shell' '
	run_timeout 60 flux run -n4 -N1 ${threadtest} 8 5
'
test_expect_success 'threadtest: split, 4 threads x 10 rounds on 4 shells' '
	run_timeout 60 flux run -n8 -N4 ${threadtest} 4 10 split
'
test_expect_success 'threadtest: split, 6 threads x 5 rounds on 1 shell' '
	run_timeout 60 flux run -n4 -N1 ${threadtest} 6 5 split
'
test_expect_success 'threadtest: node, 4 threads x 10 rounds on 4 shells' '
	run_timeout 60 flux run -n8 -N4 ${threadtest} 4 10 node
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
# --timeout
test_expect_success 'flux-pmi --timeout accepts FSD' '
	flux run -o pmi=single \
	    flux pmi --verbose --timeout=43m barrier
'
test_expect_success 'flux-pmi --timeout fails with invalid FSD' '
	test_must_fail flux run \
	    flux pmi --timeout=zzz barrier
'
test_expect_success 'flux-pmi --timeout works' '
	cat >oneslow.sh <<-EOT &&
	#!/bin/sh
	test "\$(flux getattr rank)" = "1" && sleep 30
	flux pmi --method=simple --verbose --timeout=1s barrier
	if test $? -ne 0; then
	    sleep 1
	    exit 1
	fi
	EOT
	chmod +x oneslow.sh &&
	test_must_fail_or_be_terminated \
	    flux run -l -opmi=simple -o exit-timeout=1s -N2 ./oneslow.sh
'
test_done
