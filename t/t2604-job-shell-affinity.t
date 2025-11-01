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
test_expect_success 'flux-shell: map affinity can use a mix of inputs' '
	id=$(flux submit --label-io -o cpu-affinity="map:0xf,0xf;0-3" -n 2 \
	hwloc-bind --get) &&
	flux job attach $id >map4.out 2>&1 &&
	test_debug "cat map4.out"
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
#
# verbose, dry-run tests:
#
test_expect_success 'flux-shell: cpu-affinity=verbose prints cpuset' '
	flux run -n1 -c1 -ocpu-affinity=verbose hostname >verbose.out 2>&1 &&
	test_debug "cat verbose.out" &&
	grep "cpus:" verbose.out
'
test_expect_success MULTICORE 'flux-shell: cpu-affinity=verbose,per-task prints per-task cpusets' '
	flux run --label-io -n2 -c1 \
	    -ocpu-affinity=verbose,per-task \
	    hostname >verbose-per-task.out 2>&1 &&
	test_debug "cat verbose-per-task.out" &&
	grep "cpus:" verbose-per-task.out &&
	grep "task 0: cpus:" verbose-per-task.out &&
	grep "task 1: cpus:" verbose-per-task.out
'
test_expect_success 'flux-shell: cpu-affinity=dry-run prints but does not bind' '
	hwloc-bind --get > no-bind.expected &&
	flux run -n1 -c1 -ocpu-affinity=dry-run \
	    hwloc-bind --get >dry-run.out &&
	test_debug "cat dry-run.out" &&
	test_cmp no-bind.expected dry-run.out
'
test_expect_success MULTICORE 'flux-shell: cpu-affinity=dry-run,per-task prints without binding' '
	flux run --label-io -n2 -c1 \
	    -ocpu-affinity=dry-run,per-task \
	    hwloc-bind --get >dry-run-per-task.out 2>&1 &&
	test_debug "cat dry-run-per-task.out" &&
	grep "task 0: cpus:" dry-run-per-task.out &&
	grep "task 1: cpus:" dry-run-per-task.out &&
	grep "0: $(cat no-bind.expected)" dry-run-per-task.out &&
	grep "1: $(cat no-bind.expected)" dry-run-per-task.out
'
test_expect_success MULTICORE 'flux-shell: cpu-affinity=dry-run,verbose works' '
	flux run -n1 -c2 -ocpu-affinity=dry-run,verbose \
	    hostname >dry-run-verbose.out 2>&1 &&
	test_debug "cat dry-run-verbose.out" &&
	grep "cpus:" dry-run-verbose.out
'
test_expect_success MULTICORE 'flux-shell: cpu-affinity handles verbose and map option' '
	flux run -n1 -c1 -ocpu-affinity=verbose,map:0 \
	    hostname >verbose-map.out 2>&1 &&
	test_debug "cat verbose-map.out" &&
	grep "cpus: 0" verbose-map.out
'
#
# Issue #7174 (unequal distribution of tasks across nodes)
test_expect_success MULTICORE 'flux-shell: issue #7174 reproducer' '
	flux run -N2 -n3 -c1 -o cpu-affinity=verbose hostname \
	    >7174.out 2>&1 &&
	grep "adjusted cores from 0-1 to 0" 7174.out
'
test_expect_success MULTICORE 'flux-shell: no #7174 workaround with node exclusive' '
	flux run -xN2 -n3 -c1 -o cpu-affinity=verbose hostname \
	    >7174-exclusive.out 2>&1 &&
	test_must_fail grep "adjusted cores" 7174-exclusive.out
'
test_expect_success MULTICORE 'flux-shell: issue #7174 reproducer with per-task' '
	flux run -N2 -n3 -c1 -o verbose -o cpu-affinity=verbose,per-task true \
	    >7174-per-task.out 2>&1 &&
	grep "adjusted cores from 0-1 to 0" 7174-per-task.out &&
	grep "task 2: cpus: 0" 7174-per-task.out
'
#
# GPU tests:
#
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
#  GPU affinity tests use alloc-bypass shell since simple-sched doesnt
#   schedule GPUs.
#
test_expect_success 'flux-shell: load alloc-bypass jobtap plugin' '
	flux jobtap load alloc-bypass.so
'
test_expect_success 'flux-shell: create multi-gpu R' '
	cat >R.gpu <<-EOF
	{
	  "version": 1,
	  "execution": {
	    "R_lite": [
	      {
		"children": {
		  "core": "0-1",
		  "gpu": "0-3"
		},
		"rank": "0"
	      }
	    ],
	    "nodelist": [
	      "$(hostname)"
	    ]
	  }
	}
	EOF
'
test_expect_success 'flux-shell: gpu-affinity works by default' '
	name=gpu-basic &&
	flux run -N1 -n2 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R.gpu)" \
		printenv CUDA_VISIBLE_DEVICES >${name}.output 2>${name}.err &&
	cat >${name}.expected <<-EOF  &&
	0: 0,1,2,3
	1: 0,1,2,3
	EOF
	test_debug "cat ${name}.output ${name}.err" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity=on' '
	name=gpu-on &&
	flux run -N1 -n2 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R.gpu)" \
		-o gpu-affinity=on \
		printenv CUDA_VISIBLE_DEVICES >${name}.output 2>${name}.err &&
	cat >${name}.expected <<-EOF  &&
	0: 0,1,2,3
	1: 0,1,2,3
	EOF
	test_debug "cat ${name}.output ${name}.err" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity=per-task' '
	name=gpu-per-task &&
	flux run -N1 -n2 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R.gpu)" \
		-o gpu-affinity=per-task \
		printenv CUDA_VISIBLE_DEVICES >${name}.output 2>${name}.err &&
	cat >${name}.expected <<-EOF  &&
	0: 0,1
	1: 2,3
	EOF
	test_debug "cat ${name}.output ${name}.err" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity=map: works' '
	name=gpu-map &&
	flux run -N1 -n2 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R.gpu)" \
		-o gpu-affinity="map:7;4-6" \
		printenv CUDA_VISIBLE_DEVICES >${name}.output 2>${name}.err &&
	cat >${name}.expected <<-EOF  &&
	0: 7
	1: 4,5,6
	EOF
	test_debug "cat ${name}.output ${name}.err" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out

'
test_expect_success 'flux-shell: gpu-affinity=off' '
	name=gpu-off &&
	test_expect_code 1 flux run -N1 -n2 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R.gpu)" \
		-o gpu-affinity=off \
		--env=-CUDA_VISIBLE_DEVICES \
		printenv CUDA_VISIBLE_DEVICES >${name}.output 2>${name}.err &&
	cat >${name}.expected <<-EOF  &&
	EOF
	test_debug "cat ${name}.output ${name}.err" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity bad arg is ignored' '
	name=gpu-bad-arg &&
	flux run -N1 -n2 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R.gpu)" \
		-o gpu-affinity="[1]" \
		printenv CUDA_VISIBLE_DEVICES >${name}.out 2>${name}.err &&
	test_debug "cat ${name}.out" &&
	test_debug "cat ${name}.err" >&2 &&
	grep "Failed to get gpu-affinity shell option" ${name}.err
'
test_expect_success 'flux-shell: create multi-node multi-gpu R' '
	cat >R2.gpu <<-EOF
	{
	  "version": 1,
	  "execution": {
	    "R_lite": [
	      {
		"children": {
		  "core": "0-1",
		  "gpu": "0-1"
		},
		"rank": "0-1"
	      }
	    ],
	    "nodelist": [
	      "$(hostname),$(hostname)"
	    ]
	  }
	}
	EOF
'
test_expect_success 'flux-shell: gpu-affinity per-task with uneven distribution' '
	name=gpu-per-task-xtra &&
	flux run -N2 -n3 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R2.gpu)" \
		-o gpu-affinity=per-task \
		printenv CUDA_VISIBLE_DEVICES >${name}.output &&
	cat >${name}.expected <<-EOF  &&
	0: 0
	1: 1
	2: 0
	EOF
	test_debug "cat ${name}.output" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out
'
test_expect_success 'flux-shell: gpu-affinity uneven distribution node-exclusive' '
	name=gpu-per-task-exclusive &&
	flux run -xN2 -n3 \
		--label-io \
		--setattr=alloc-bypass.R="$(cat R2.gpu)" \
		-o gpu-affinity=per-task \
		printenv CUDA_VISIBLE_DEVICES >${name}.output &&
	cat >${name}.expected <<-EOF  &&
	0: 0
	1: 1
	2: 0,1
	EOF
	test_debug "cat ${name}.output" &&
	sort -k1,1n ${name}.output > ${name}.out &&
	test_cmp ${name}.expected ${name}.out
'
test_done
