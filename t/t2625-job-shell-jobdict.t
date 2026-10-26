#!/bin/sh
#
test_description='Test flux-shell builtin jobdict plugin'

. `dirname $0`/sharness.sh

test_under_flux 4 job

PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"

# The jobdict-test plugin drives the jobdict through the plugin stack as an
# external plugin and raises a job exception if any check fails.
cat >jobdict.lua <<EOT
plugin.searchpath = "${PLUGINPATH}"
plugin.load { file = "jobdict.so" }
EOT

run_jobdict_test() {
	run_timeout 30 flux run -o verbose=1 -o userrc=$(pwd)/jobdict.lua "$@" \
		true 2>&1
}

test_expect_success 'jobdict test plugin passes on one shell' '
	run_jobdict_test -N1 -n1 >one.out &&
	test_debug "cat one.out" &&
	grep -c "full1 checks passed" one.out | grep -x 1 &&
	grep -c "subset checks passed" one.out | grep -x 1 &&
	grep -c "full2 checks passed" one.out | grep -x 1
'
test_expect_success 'jobdict test plugin passes on four shells' '
	run_jobdict_test -N4 -n4 >four.out &&
	test_debug "cat four.out" &&
	grep -c "full1 checks passed" four.out | grep -x 4 &&
	grep -c "subset checks passed" four.out | grep -x 1 &&
	grep -c "full2 checks passed" four.out | grep -x 4
'
test_expect_success 'jobdict test plugin passes with fanout k=1' '
	run_jobdict_test -N4 -n4 -o jobdict-test-k=1 >k1.out &&
	grep -c "full2 checks passed" k1.out | grep -x 4
'
test_expect_success 'jobdict test plugin passes with fanout k=4' '
	run_jobdict_test -N4 -n4 -o jobdict-test-k=4 >k4.out &&
	grep -c "full2 checks passed" k4.out | grep -x 4
'
test_expect_success 'jobdict test plugin passes with fanout larger than job' '
	run_jobdict_test -N4 -n4 -o jobdict-test-k=8 >k8.out &&
	grep "requested fanout too large, using k=4" k8.out &&
	grep -c "full2 checks passed" k8.out | grep -x 4
'
# Shell 0 never creates the dict, so with k=1 (a chain 0-1-2-3) shell 1
# gets ENOENT and the failure cascades to shells 2 and 3.
test_expect_success 'exchange error propagates down the tree' '
	run_jobdict_test -N4 -n4 -o jobdict-test-k=1 \
		-o jobdict-test-mode=chain-error >chain.out &&
	test_debug "cat chain.out" &&
	grep "flux-shell\[1\].*No such file or directory" chain.out &&
	grep -c "exchange chain failed as expected" chain.out | grep -x 3
'
# Shells 1-3 enter T with all shells as participants before shell 0
# enters it with participants 0-1, so shell 0 has two queued child
# requests but expects one.  Its enter fails and the children are failed.
test_expect_success 'session is discarded when enter finds excess requests' '
	run_jobdict_test -N4 -n4 -o jobdict-test-k=2 \
		-o jobdict-test-mode=eproto >eproto.out &&
	test_debug "cat eproto.out" &&
	grep "2 child requests queued for T but 1 expected" eproto.out &&
	grep "exchange T enter failed as expected" eproto.out &&
	grep -c "exchange T failed as expected" eproto.out | grep -x 3
'
test_expect_success 'concurrent exchanges each carry their own snapshot' '
	run_jobdict_test -N4 -n4 -o jobdict-test-mode=concurrent >cc.out &&
	test_debug "cat cc.out" &&
	grep -c "concurrent checks passed" cc.out | grep -x 4
'
test_expect_success 'flux jobdict holds flux.taskmap and flux.instance-level' '
	id=$(flux submit -N2 -n4 -o verbose=1 -o userrc=$(pwd)/jobdict.lua \
		--taskmap=cyclic true) &&
	run_timeout 30 flux job attach $id >flux.out 2>&1 &&
	test_debug "cat flux.out" &&
	level=$(flux getattr instance-level) &&
	grep -c "flux.instance-level=$((level+1))" flux.out | grep -x 2 &&
	grep "flux-shell\[0\].*flux.taskmap=" flux.out \
		| sed "s/.*flux.taskmap=//" >taskmap.out &&
	flux job taskmap $id >taskmap.exp &&
	jq -c .map taskmap.out >taskmap.map &&
	test_cmp taskmap.exp taskmap.map
'
test_expect_success 'jobdict coexists with the PMI jobdict' '
	run_timeout 30 flux run -N4 -n4 -o userrc=$(pwd)/jobdict.lua \
		${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest
'
test_done
