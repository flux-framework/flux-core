#!/bin/sh

test_description='Test flux run command'

. $(dirname $0)/sharness.sh

test_under_flux 4

# Set CLIMain log level to logging.DEBUG (10), to enable stack traces
export FLUX_PYCLI_LOGLEVEL=10

flux setattr log-stderr-level 1

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

test_expect_success 'flux run fails with error message' '
	test_must_fail flux run 2>usage.err &&
	grep "job command and arguments are missing" usage.err
'
test_expect_success 'flux run ignores ambiguous args after --' '
	flux run -n2 --dry-run -- hostname --n=2 \
		| jq -e ".tasks[0].command[0] == \"hostname\""
'
test_expect_success 'flux run works' '
	flux run hostname >run.out &&
	hostname >run.exp &&
	test_cmp run.exp run.out
'
test_expect_success 'flux run --ntasks=2 works' '
	flux run --ntasks=2 hostname >run2.out &&
	(hostname;hostname) >run2.exp &&
	test_cmp run2.exp run2.out
'
test_expect_success 'flux run --ntasks=2 --label-io works' '
	flux run --ntasks=2 --label-io echo Hi |sort >run2l.out &&
	cat >run2l.exp <<-EOT &&
	0: Hi
	1: Hi
	EOT
	test_cmp run2l.exp run2l.out
'
test_expect_success 'flux run --ntasks=2 --nodes=2 works' '
	flux run --ntasks=2 --nodes=2 hostname
'
test_expect_success 'flux run --ntasks=1 --nodes=2 fails' '
	test_must_fail flux run --ntasks=1 --nodes=2 hostname \
		2>run1n2N.err &&
	grep -i "node count must not be greater than task count" run1n2N.err
'
test_expect_success 'flux run -v produces jobid on stderr' '
	flux run -v hostname 2>v.err &&
	grep jobid: v.err
'
test_expect_success 'flux run -vv produces job events on stderr' '
	flux run -vv hostname 2>vv.err &&
	grep submit vv.err
'
test_expect_success 'flux run -vvv produces exec events on stderr' '
	flux run -vvv hostname 2>vvv.err &&
	grep complete vvv.err
'
test_expect_success 'flux run waits for clean event by default' '
	grep clean vvv.err
'
test_expect_success 'flux run --cwd works' '
	mkdir cwd_test &&
	flux run --cwd=$(realpath cwd_test) pwd > cwd.out &&
	test $(cat cwd.out) = $(realpath cwd_test)
'
test_expect_success 'flux run --env=VAR=${VAL:-default} fails' '
	test_expect_code 1 flux run --dry-run \
	    --env=* --env=VAR=\${VAL:-default} hostname >env-fail.err 2>&1 &&
	test_debug "cat env-fail.err" &&
	grep "Unable to substitute" env-fail.err
'
test_expect_success 'flux run --env=VAR=$VAL fails when VAL not in env' '
	unset VAL &&
	test_expect_code 1 flux run --dry-run \
	    --env=* --env=VAR=\$VAL hostname >env-notset.err 2>&1 &&
	test_debug "cat env-notset.err" &&
	grep "env: Variable .* not found" env-notset.err
'
test_expect_success 'flux run propagates some rlimits by default' '
	flux run --dry-run hostname | \
	    jq .attributes.system.shell.options.rlimit >rlimit-default.out &&
	# check random sample of rlimits:
	grep core rlimit-default.out &&
	grep stack rlimit-default.out &&
	grep nofile rlimit-default.out
'
test_expect_success 'flux run --rlimit=-* works' '
	flux run --rlimit=-* --dry-run hostname \
	    | jq -e ".attributes.system.shell.options.rlimit == null"
'
test_expect_success 'flux run --rlimit=name works' '
	flux run --rlimit=memlock --dry-run hostname \
	    | jq .attributes.system.shell.options.rlimit >rlimit-memlock.out &&
	grep memlock rlimit-memlock.out &&
	grep core rlimit-memlock.out
'
test_expect_success 'flux run --rlimit=name --rlimit=name works' '
	flux run --rlimit=memlock --rlimit=ofile --dry-run hostname \
	    | jq .attributes.system.shell.options.rlimit >rlimit-ofile.out &&
	grep memlock rlimit-memlock.out &&
	grep ofile rlimit-memlock.out
'
test_expect_success 'flux run --rlimit=-*,core works' '
	flux run --rlimit=-*,core --dry-run hostname \
	    | jq .attributes.system.shell.options.rlimit >rlimit-core.out &&
	grep core rlimit-core.out &&
	test_must_fail grep nofile rlimit-core.out
'
test_expect_success 'flux run --rlimit=name=value works' '
	flux run --rlimit=core=16 --dry-run hostname \
	    | jq -e ".attributes.system.shell.options.rlimit.core == 16" &&
	inf=$(flux python -c "import resource as r; print(r.RLIM_INFINITY)") &&
	flux run --rlimit=core=unlimited --dry-run hostname \
	    | jq -e ".attributes.system.shell.options.rlimit.core == $inf"
'
test_expect_success 'flux run --rlimit=invalid fails with error' '
	test_must_fail flux run --rlimit=frobnitz hostname
'
test_expect_success 'flux run --rlimit=core=invalid fails with error' '
	test_must_fail flux run --rlimit=core=invalid hostname
'

#  Per-resource expected failure tests
cat <<EOF >per-resource-failure.txt
\
--tasks-per-core=1 --tasks-per-node=1 \
==Do not specify both the number of tasks per node and per core
\
--tasks-per-node=0 \
==--tasks-per-node must be >= 1
\
--tasks-per-core=0 \
==--tasks-per-core must be >= 1
\
--cores=-4 \
==ncores must be an integer >= 1
\
--nodes=1 --gpus-per-node=-1 \
==gpus_per_node must be an integer >= 0
\
--gpus-per-node=1 \
==gpus-per-node requires --nodes
\
--cores=4 --exclusive \
==exclusive can only be set with a node count
\
--nodes=4 --cores=2 \
==number of cores cannot be less than nnodes
\
--nodes=5 --cores=9 \
==number of cores must be evenly divisible by node count
\
--cores=2 --cores-per-task=1 \
==Per-resource options.*per-task options
\
--cores=1 --ntasks=1 \
==Per-resource options.*per-task options
\
--nodes=1 --tasks-per-node=1 --gpus-per-task=1 \
==Per-resource options.*per-task options
\
--nodes=1 --tasks-per-core=1 --cores-per-task=1 \
==Per-resource options.*per-task options
\
--tasks-per-core=1 \
==must specify node or core count with per_resource
\
--tasks-per-node=1 \
==must specify node or core count with per_resource
EOF

while read line; do
	args=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
	expected=$(echo $line | awk -F== '{print $2}')
	test_expect_success "per-resource: $args error: $expected" '
		output=per-resource-error.${test_count}.out &&
		test_must_fail flux run $args --env=-* --dry-run hostname \
			>${output} 2>&1 &&
		test_debug "cat $output" &&
		grep -- "$expected" $output
	'
done < per-resource-failure.txt


#  Per-resource expected success tests
cat <<EOF >per-resource-args.txt
\
-N2 --cores=2 \
==nnodes=2 nslots=2 slot_size=1 slot_gpus=0 exclusive=false duration=0.0 \
==
\
-N2 --cores=2 --tasks-per-node=2 \
==nnodes=2 nslots=2 slot_size=1 slot_gpus=0 exclusive=false duration=0.0 \
=={"type": "node", "count": 2}
\
-N2 --cores=2 --tasks-per-core=2 \
==nnodes=2 nslots=2 slot_size=1 slot_gpus=0 exclusive=false duration=0.0 \
=={"type": "core", "count": 2}
\
--cores=16 \
==nnodes=0 nslots=16 slot_size=1 slot_gpus=0 exclusive=false duration=0.0 \
==
\
--cores=16 --tasks-per-node=1 \
==nnodes=0 nslots=16 slot_size=1 slot_gpus=0 exclusive=false duration=0.0 \
=={"type": "node", "count": 1}
\
--cores=5 --tasks-per-core=2 \
==nnodes=0 nslots=5 slot_size=1 slot_gpus=0 exclusive=false duration=0.0 \
=={"type": "core", "count": 2}
\
--nodes=2 --tasks-per-node=2 \
==nnodes=2 nslots=2 slot_size=1 slot_gpus=0 exclusive=true duration=0.0 \
=={"type": "node", "count": 2}
\
--nodes=2 --tasks-per-core=1 \
==nnodes=2 nslots=2 slot_size=1 slot_gpus=0 exclusive=true duration=0.0 \
=={"type": "core", "count": 1}
\
-N1 --gpus-per-node=2 \
==nnodes=1 nslots=1 slot_size=1 slot_gpus=2 exclusive=true duration=0.0 \
==
\
-N2 --cores=4 --tasks-per-node=1 --gpus-per-node=1 \
==nnodes=2 nslots=2 slot_size=2 slot_gpus=1 exclusive=false duration=0.0 \
=={"type": "node", "count": 1}
EOF

jj=${FLUX_BUILD_DIR}/t/sched-simple/jj-reader
while read line; do
	args=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
	expected=$(echo $line | awk -F== '{print $2}')
	per_resource=$(echo $line | awk -F== '{print $3}' | sed 's/  *$//')
	test_expect_success "per-resource: $args" '
	    echo $expected >expected.$test_count &&
	    flux run $args --dry-run --env=-* hostname > jobspec.$test_count &&
	    $jj < jobspec.$test_count >output.$test_count &&
	    test_debug "cat output.$test_count" &&
	    test_cmp expected.$test_count output.$test_count &&
	    if test -n "$per_resource"; then
	        test_debug "echo expected $per_resource" &&
	        jq -e ".attributes.system.shell.options.\"per-resource\" == \
		   $per_resource" < jobspec.$test_count
	    fi
	'
done < per-resource-args.txt

test_expect_success NO_CHAIN_LINT 'flux run --unbuffered works for stdin/out' '
        rm -f fifo &&
        mkfifo fifo &&
        (flux run --unbuffered cat >unbuffered.out <fifo &) &&
        exec 9>fifo &&
        printf prompt: >&9 &&
        $waitfile -v --count=1 --timeout=10 --pattern=prompt: unbuffered.out &&
        exec 9<&- &&
        rm -f fifo &&
        flux job wait-event -vt 15 $(flux job last) clean
'
test_expect_success 'flux run --input=ranks works for stdin' '
	echo hi | flux run --label-io --input=0 -n4 cat >input-test.out 2>&1 &&
	cat <<-EOF >input-test.expected &&
	0: hi
	EOF
	test_cmp input-test.expected input-test.out
'
test_done
