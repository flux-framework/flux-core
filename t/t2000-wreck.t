#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE}

#  Return the previous jobid
last_job_id() {
	local n=$(flux kvs get "lwj.next-id")
	echo $((n-1))
}

test_expect_success 'wreckrun: works' '
	hostname=$(hostname) &&
	run_timeout 5 flux wreckrun -n${SIZE} hostname  >output &&
	for i in $(seq 1 ${SIZE}); do echo $hostname; done >expected &&
	test_cmp expected output
'

test_expect_success 'wreckrun: propagates current working directory' '
	mkdir -p testdir &&
	mypwd=$(pwd)/testdir &&
	( cd testdir &&
	run_timeout 5 flux wreckrun -N1 -n1 pwd ) | grep "^$mypwd$"
'
test_expect_success 'wreckrun: propagates current environment' '
	( export MY_UNLIKELY_ENV=0xdeadbeef &&
	  run_timeout 5 flux wreckrun -N1 -n1 env ) | \
           grep "MY_UNLIKELY_ENV=0xdeadbeef"
'
test_expect_success 'wreckrun: does not propagate FLUX_URI' '
	run_timeout 5 flux wreckrun -n${SIZE} printenv FLUX_URI >uri_output &&
	test `sort uri_output | uniq | wc -l` -eq ${SIZE}
'
test_expect_success 'wreckrun: does not drop output' '
	for i in `seq 0 100`; do 
		base64 /dev/urandom | head -c77
	done >expected &&
	run_timeout 5 flux wreckrun -N1 -n1 cat expected >output &&
	test_cmp expected output
'
test_expect_success 'wreck: job state events emitted' '
	run_timeout 5 \
	  $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
	   wreck.state wreck.state.complete \
	   flux wreckrun -n${SIZE} /bin/true > output &&
        tail -4 output > output_states && # only care about last 4
	cat >expected_states <<-EOF &&
	wreck.state.reserved
	wreck.state.starting
	wreck.state.running
	wreck.state.complete
	EOF
	test_cmp expected_states output_states
'
test_expect_success 'wreck: signaling wreckrun works' '
        flux wreckrun -n${SIZE} sleep 15 </dev/null &
	q=$! &&
	$SHARNESS_TEST_SRCDIR/scripts/event-trace.lua \
           wreck.state wreck.state.running /bin/true &&
        sleep 0.5 &&
	kill -INT $q &&
	test_expect_code 137 wait $q
'
flux kvs dir -r resource >/dev/null 2>&1 && test_set_prereq RES_HWLOC
test_expect_success RES_HWLOC 'wreckrun: oversubscription of tasks' '
	run_timeout 15 flux wreckrun -v -n$(($(nproc)*${SIZE}+1)) /bin/true
'
test_expect_success 'wreckrun: uneven distribution with -n, -N' '
	run_timeout 10 flux wreckrun -N${SIZE} -n$((${SIZE}+1)) /bin/true
'
test_expect_success 'wreckrun: too many nodes requested fails' '
	test_expect_code 1 run_timeout 10 flux wreckrun -N$((${SIZE}+1)) hostname
'
test_expect_success 'wreckrun: no nnodes or ntasks args runs one task on rank 0' '
	test "$(flux wreckrun -l hostname)" = "0: $hostname"
'
test_expect_success 'wreckrun: -n1 runs one task on rank 0' '
	test "$(flux wreckrun -l hostname)" = "0: $hostname"
'
test_expect_success 'wreckrun: -n divides tasks among ranks' '
	flux wreckrun -l -n$((${SIZE}*2)) printenv FLUX_NODE_ID | sort >output_nx2 &&
        i=0
	for n in $(seq 0 $((${SIZE}-1))); do
		echo "$i: $n"; echo "$((i+1)): $n";
		i=$((i+2));
	done >expected_nx2 &&
	test_cmp expected_nx2 output_nx2
'
test_expect_success 'wreckrun: -N without -n works' '
	flux wreckrun -l -N${SIZE} printenv FLUX_NODE_ID | sort >output_N &&
	for n in $(seq 0 $((${SIZE}-1))); do
		echo "$n: $n";
		i=$((i+2));
	done >expected_N &&
	test_cmp expected_N output_N
'
test_expect_success 'wreckrun: -N without -n sets ntasks in kvs' '
	flux wreckrun -l -N${SIZE} /bin/true &&
	LWJ=$(last_job_id) &&
	n=$(flux kvs get lwj.${LWJ}.ntasks) &&
	test "$n" = "${SIZE}"
'
test_expect_success 'wreckrun: -n without -N sets nnnodes in kvs' '
	flux wreckrun -l -n${SIZE} /bin/true &&
	LWJ=$(last_job_id) &&
	n=$(flux kvs get lwj.${LWJ}.nnodes) &&
	test "$n" = "${SIZE}"
'

cpus_allowed() {
	${SHARNESS_TEST_SRCDIR}/scripts/cpus-allowed.lua "$@"
}
test "$(cpus_allowed count)" = "0" || test_set_prereq MULTICORE

test_expect_success MULTICORE 'wreckrun: supports affinity assignment' '
	newmask=$(cpus_allowed last) &&
	run_timeout 5 flux wreckrun -n1 \
	  --pre-launch-hook="lwj.rank[0].cpumask = \"$newmask\"" \
	  grep ^Cpus_allowed_list /proc/self/status > output_cpus &&
	cat <<-EOF >expected_cpus &&
	Cpus_allowed_list:	$newmask
	EOF
	test_cmp expected_cpus output_cpus
'
test_expect_success MULTICORE 'wreckrun: supports per-task affinity assignment' '
	mask=$(cpus_allowed) &&
	newmask=$(cpus_allowed first) &&
	run_timeout 5 flux wreckrun -ln2 \
	  --pre-launch-hook="lwj[\"0.cpumask\"] = \"$newmask\"" \
	  grep ^Cpus_allowed_list /proc/self/status | sort > output_cpus2 &&
	cat <<-EOF >expected_cpus2 &&
	0: Cpus_allowed_list:	$newmask
	1: Cpus_allowed_list:	$mask
	EOF
	test_cmp expected_cpus output_cpus
'
test_expect_success 'wreckrun: top level environment' '
	flux kvs put lwj.environ="{ \"TEST_ENV_VAR\": \"foo\" }" &&
	run_timeout 5 flux wreckrun -n2 printenv TEST_ENV_VAR > output_top_env &&
	cat <<-EOF >expected_top_env &&
	foo
	foo
	EOF
	test_cmp expected_top_env output_top_env &&
	TEST_ENV_VAR=bar \
	  flux wreckrun -n2 printenv TEST_ENV_VAR > output_top_env2 &&
	cat <<-EOF >expected_top_env2 &&
	bar
	bar
	EOF
	test_cmp expected_top_env2 output_top_env2
'
test_expect_success 'wreck plugins can use wreck:log_msg()' '
	saved_pattern=$(flux kvs get config.wrexec.lua_pattern) &&
	cleanup flux kvs put wrexec.lua_pattern="$saved_pattern" &&
	cat <<-EOF >test.lua &&
	function rexecd_init ()
	    local rc, err = wreck:log_msg ("lwj.%d: plugin test successful", wreck.id)
	    if not rc then error (err) end
	end
	EOF
	flux kvs put "config.wrexec.lua_pattern=$(pwd)/*.lua" &&
	flux wreckrun /bin/true &&
	flux dmesg | grep "plugin test successful" || (flux dmesg | grep lwj\.$(last_job_id) && false)
'

test_done
