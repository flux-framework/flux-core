#!/bin/sh
#
test_description='Test flux-shell default affinity implementation'

. `dirname $0`/sharness.sh

test_under_flux 2

if ! which hwloc-bind > /dev/null; then
    skip_all='skipping affinity tests since hwloc-bind not found'
    test_done
fi

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"
CPUS_ALLOWED_COUNT="$(pwd)/cpus-allowed-count.sh"

cat >${CPUS_ALLOWED_COUNT} << EOF
#!/bin/sh
hwloc-bind --get | hwloc-calc --number-of core | tail -n 1
EOF
chmod +x ${CPUS_ALLOWED_COUNT}

test $(${CPUS_ALLOWED_COUNT}) = 1 || test_set_prereq MULTICORE

echo >&2 "# Running tests on $($CPUS_ALLOWED_COUNT) cores"

test_expect_success 'flux-shell: affinity hwloc-calc works' '
    hwloc-bind --get &&
    hwloc-bind --get | hwloc-calc --number-of core &&
    hwloc-bind --get | hwloc-calc --number-of pu
'
test_expect_success 'flux-shell: default affinity works (1 core)' '
    flux run -n1 -c1 $CPUS_ALLOWED_COUNT > result.n1 &&
    test_debug "cat result.n1" &&
    test "$(cat result.n1)" = "1"
'
test_expect_success MULTICORE 'flux-shell: default affinity works (2 cores)' '
    flux run -n1 -c2 $CPUS_ALLOWED_COUNT > result.n1 &&
    test_debug "cat result.n1" &&
    test "$(cat result.n1)" = "2"
'
test_expect_success MULTICORE 'flux-shell: per-task affinity works' '
    flux run --label-io -ocpu-affinity=per-task -n2 -c1 \
		hwloc-bind --get > per-task.out &&
    task0set=$(sed -n "s/^0: //p" per-task.out) &&
    task1set=$(sed -n "s/^1: //p" per-task.out) &&
    test_debug "echo checking ${task0set} not equal ${task1set}" &&
    test "$task0set" != "$task1set"
'
test_expect_success 'flux-shell: per-task affinity sanity check' '
    flux run --label-io -ocpu-affinity=per-task -n1 -c1 \
		hwloc-bind --get
'
test_expect_success MULTICORE 'flux-shell: map affinity works' '
    flux run --label-io -o cpu-affinity="map:1;0" -n 2 \
	hwloc-bind --get > map1.out &&
    task0set=$(sed -n "s/^0: //p" map1.out) &&
    task1set=$(sed -n "s/^1: //p" map1.out) &&
    test_debug "echo checking ${task0set}=0x2 ${task1set}=0x1" &&
    test "$(hwloc-calc --taskset $task0set)" = "0x2" &&
    test "$(hwloc-calc --taskset $task1set)" = "0x1"
'
test_expect_success MULTICORE 'flux-shell: map affinity reuses underspecified sets' '
    flux run --label-io -o cpu-affinity=map:1 -n 2 \
	hwloc-bind --get > map2.out &&
    task0set=$(sed -n "s/^0: //p" map2.out) &&
    task1set=$(sed -n "s/^1: //p" map2.out) &&
    test_debug "echo checking ${task0set}=0x2 ${task1set}=0x2" &&
    test "$(hwloc-calc --taskset $task0set)" = "0x2" &&
    test "$(hwloc-calc --taskset $task1set)" = "0x2"
'
test_expect_success MULTICORE 'flux-shell: map affinity can use hex bitmasks' '
    flux run --label-io -o cpu-affinity="map:0x1;0x2" -n 2 \
	hwloc-bind --get > map3.out &&
    task0set=$(sed -n "s/^0: //p" map3.out) &&
    task1set=$(sed -n "s/^1: //p" map3.out) &&
    test_debug "echo checking ${task0set}=0x1 ${task1set}=0x2" &&
    test "$(hwloc-calc --taskset $task0set)" = "0x1" &&
    test "$(hwloc-calc --taskset $task1set)" = "0x2"
'
# Expected to fail since 0xf,0xf won't be in the job cpuset, we're just
# testing the parsing of args now
test_expect_success 'flux-shell: map affinity can use a mix of inputs' '
    id=$(flux submit --label-io -o cpu-affinity="map:0xf,0xf;0-3" -n 2 \
	hwloc-bind --get) &&
    test_must_fail flux job attach $id >map4.out 2>&1 &&
    test_debug "cat map4.out" &&
    grep "cpuset 0xf,0xf is not included in job cpuset" map4.out
'
test_expect_success 'flux-shell: invalid cpuset is detected' '
    test_must_fail flux run -o cpu-affinity="map:0x0;1" -n 2 \
        hwloc-bind --get
'
test_expect_success 'flux-shell: affinity can be disabled' '
    hwloc-bind --get > affinity-off.expected &&
    flux run -ocpu-affinity=off -n1 hwloc-bind --get >affinity-off.out &&
    test_cmp affinity-off.expected affinity-off.out
'
test_expect_success 'flux-shell: invalid option is ignored' '
    flux run -ocpu-affinity=1 -n1 hwloc-bind --get >invalid.out 2>&1 &&
    test_debug "cat invalid.out" &&
    grep "invalid option" invalid.out
'
test_expect_success 'flux-shell: CUDA_VISIBLE_DEVICES=-1 set by default' '
    flux run printenv CUDA_VISIBLE_DEVICES >default-gpubind.out 2>&1 &&
    test_debug "cat default-gpubind.out" &&
    grep "^-1" default-gpubind.out
'
test_expect_success 'flux-shell: CUDA_VISIBLE_DEVICES=-1 works with existing value' '
    CUDA_VISIBLE_DEVICES=0,1 \
       flux run printenv CUDA_VISIBLE_DEVICES >override-gpubind.out 2>&1 &&
    test_debug "cat override-gpubind.out" &&
    grep "^-1" override-gpubind.out
'
#  GPU affinity tests use standalone shell since simple-sched doesnt
#   schedule GPUs.
#
test_expect_success 'flux-shell: create multi-gpu R' '
	cat >R.gpu <<-EOF
	        {"version": 1, "execution":{ "R_lite":[
                { "children": { "core": "0-1", "gpu": "0-3" }, "rank": "0" }
        ]}}
	EOF
'
test_expect_success 'flux-shell: gpu-affinity works by default' '
	name=gpu-basic &&
	flux run -N1 -n2 --dry-run \
		printenv CUDA_VISIBLE_DEVICES > j.${name} &&
	cat >${name}.expected <<-EOF  &&
	0: 0,1,2,3
	1: 0,1,2,3
	EOF
	${FLUX_SHELL} -s -v -r 0 -j j.${name} -R R.gpu 0 &&
	${FLUX_SHELL} -s -v -r 0 -j j.${name} -R R.gpu 0 | sort -k1,1n \
		> ${name}.out 2>${name}.err &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity=on' '
	name=gpu-on &&
	flux run -N1 -n2 --dry-run -o gpu-affinity=on \
		printenv CUDA_VISIBLE_DEVICES > j.${name} &&
	cat >${name}.expected <<-EOF  &&
	0: 0,1,2,3
	1: 0,1,2,3
	EOF
	${FLUX_SHELL} -s -v -r 0 -j j.${name} -R R.gpu 0 | sort -k1,1n \
		> ${name}.out 2>${name}.err &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity=per-task' '
	name=gpu-per-task &&
	flux run -N1 -n2 --dry-run -o gpu-affinity=per-task \
		printenv CUDA_VISIBLE_DEVICES > j.${name} &&
	cat >${name}.expected <<-EOF  &&
	0: 0,1
	1: 2,3
	EOF
	${FLUX_SHELL} -s -v -r 0 -j j.${name} -R R.gpu 0 | sort -k1,1n \
		> ${name}.out 2>${name}.err &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity=off' '
	name=gpu-off && (
	  unset CUDA_VISIBLE_DEVICES &&
	  flux run -N1 -n2 --dry-run -o gpu-affinity=off \
		printenv CUDA_VISIBLE_DEVICES > j.${name}
	) &&
	cat >${name}.expected <<-EOF  &&
	EOF
	test_expect_code 1  ${FLUX_SHELL} -s -v -r 0 -j j.${name} -R R.gpu 0 \
	  | sort -k1,1n \
	  > ${name}.out 2>${name}.err &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity bad arg is ignored' '
	name=gpu-bad-arg &&
	flux run -N1 -n2 --dry-run -o gpu-affinity="[1]" \
		printenv CUDA_VISIBLE_DEVICES > j.${name} &&
	${FLUX_SHELL} -s -v -r 0 -j j.${name} -R R.gpu 0 \
	  >${name}.out 2>${name}.err &&
	test_debug "cat ${name}.out" &&
	test_debug "cat ${name}.err" >&2 &&
	grep "Failed to get gpu-affinity shell option" ${name}.err
'
test_done
