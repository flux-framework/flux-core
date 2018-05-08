#!/bin/sh

test_description='Test basic jstat functionality

Test the basic functionality of job status and control API.'

. `dirname $0`/sharness.sh

test_under_flux 4 wreck
if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

tr1="null->reserved"
tr2="reserved->starting"
tr3="starting->running"
tr4="running->completing"
tr5="completing->complete"
trans="$tr1
$tr2
$tr3
$tr4
$tr5"

#  Return previous job path in kvs
last_job_path() {
   flux wreck last-jobid -p
}

run_flux_jstat () {
    ofile="output.$1"
    rm -f ${ofile}
    flux jstat -o ${ofile} notify >/dev/null &
    echo $! &&
    $SHARNESS_TEST_SRCDIR/scripts/waitfile.lua --timeout 2 ${ofile} >&2
}

wait_until_pattern () {
    local ofile="output.${1}"
    local pat="${2}"
    local cnt="${3:-1}"
    $SHARNESS_TEST_SRCDIR/scripts/waitfile.lua --timeout 2 --pattern=${pat} \
        --count=${cnt} ${ofile} >&2
}

overlap_flux_wreckruns () {
    insts=$(($1-1))
    for i in `seq 0 $insts`; do
        st=$(echo "$insts $i" | awk '{printf "%.2f", ($1 + 3 - 2*$2)/4}')
        ids="$ids $(flux wreckrun --detach -n4 -N4 sleep $st)"
    done
    for i in $ids; do
        echo "attempt to attach to job $i" >&2
        flux wreck attach $i
        echo "attach to job $i complete with code=$?" >&2
    done
    return 0
}

test_expect_success 'jstat 1: notification works for 1 wreckrun' '
    p=$(run_flux_jstat 1) &&
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&
    cat >expected1 <<-EOF &&
$trans
EOF
    wait_until_pattern 1 complete 1 &&
    cp output.1 output.1.cp &&
    kill -INT $p &&
    test_cmp expected1 output.1.cp 
'

test_expect_success 'jstat 2: jstat back-to-back works' '
    p=$(run_flux_jstat 2) &&
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&
    cat >expected2 <<-EOF &&
$trans
EOF
    wait_until_pattern 2 complete 1 &&
    cp output.2 output.2.cp &&
    kill -INT $p &&
    test_cmp expected2 output.2.cp 
'

test_expect_success 'jstat 3: notification works for multiple wreckruns' '
    p=$(run_flux_jstat 3) &&
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&
	run_timeout 4 flux wreckrun -n4 -N4 hostname &&
	run_timeout 4 flux wreckrun -n4 -N4 hostname &&
    cat >expected3 <<-EOF &&
$trans
$trans
$trans
EOF
    wait_until_pattern 3 complete 3 &&
    cp output.3 output.3.cp &&
    kill -INT $p &&
    test_cmp expected3 output.3.cp 
'

test_expect_success LONGTEST 'jstat 4: notification works under lock-step stress' '
    p=$(run_flux_jstat 4) &&
    for i in `seq 1 20`; do 
        run_timeout 4 flux wreckrun -n4 -N4 hostname 
    done &&
    cat >expected4 <<-EOF &&
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
EOF
    wait_until_pattern 4 complete 20 &&
    cp output.4 output.4.cp &&
    kill -INT $p &&
    test_cmp expected4 output.4.cp 
'

test_expect_success 'jstat 5: notification works for overlapping wreckruns' '
    p=$(run_flux_jstat 5) &&
    overlap_flux_wreckruns 3 &&
    cat >expected5 <<-EOF &&
$trans
$trans
$trans
EOF
    sort expected5 > expected5.sort &&
    wait_until_pattern 5 complete 3 &&
    cp output.5 output.5.cp &&
    sort output.5.cp > output.5.sort &&
    kill -INT $p &&
    test_cmp expected5.sort output.5.sort
'

test_expect_success LONGTEST 'jstat 6: notification works for overlapping stress' '
    p=$(run_flux_jstat 6) &&
    overlap_flux_wreckruns 20 &&
    cat >expected6 <<-EOF &&
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
$trans
EOF
    sort expected6 > expected6.sort &&
    wait_until_pattern 6 complete 20 &&
    cp output.6 output.6.cp &&
    sort output.6.cp > output.6.sort &&
    kill -INT $p &&
    test_cmp expected6.sort output.6.sort
'

test_expect_success 'jstat 7.1: run hostname to create some state' '
    run_timeout 4 flux wreckrun -n4 -N4 hostname
'

test_expect_success 'jstat 7.2: basic query works: jobid' '
    flux jstat query 1 jobid
'

test_expect_success 'jstat 7.3: basic query works: state-pair' '
    flux jstat query 1 state-pair
'

test_expect_success 'jstat 7.4: basic query works: rdesc' '
    flux jstat query 1 rdesc
'

test_expect_success 'jstat 7.5: basic query works: pdesc' '
    flux jstat query 1 pdesc
'

test_expect_success 'jstat 8: query detects bad inputs' '
    test_expect_code 42 flux jstat query 0 jobid &&
    test_expect_code 42 flux jstat query 99999 state-pair &&
    test_expect_code 42 flux jstat query 1 unknown &&
    test_expect_code 42 flux jstat query 99999 unknown
'

test_expect_success 'jstat 9: update state-pair' "
    flux jstat update 1 state-pair '{\"state-pair\": {\"ostate\": 13, \"nstate\": 12}}' &&
    flux kvs get --json $(flux wreck kvs-path 1).state > output.9.1 &&
    cat >expected.9.1 <<-EOF &&
cancelled
EOF
    test_cmp expected.9.1 output.9.1 
"

test_expect_success 'jstat 10: update procdescs' "
    flux kvs get --json $(flux wreck kvs-path 1).0.procdesc > output.10.1 &&
    flux jstat update 1 pdesc '{\"pdesc\": {\"procsize\":1, \"hostnames\":[\"0\"], \"executables\":[\"fake\"], \"pdarray\":[{\"pid\":8482,\"eindx\":0,\"hindx\":0}]}}' &&
    flux kvs get --json $(flux wreck kvs-path 1).0.procdesc > output.10.2 &&
    test_expect_code 1 diff output.10.1 output.10.2 
"

test_expect_success 'jstat 11: update rdesc' "
    flux jstat update 1 rdesc '{\"rdesc\": {\"nnodes\": 128, \"ntasks\": 128, \"ncores\":128, \"walltime\":3600}}' &&
    flux kvs get --json $(flux wreck kvs-path 1).ntasks > output.11.1 &&
    cat > expected.11.1 <<-EOF &&
128
EOF
    test_cmp expected.11.1 output.11.1 
"

test_expect_success 'jstat 12: update rdl' "
    flux jstat update 1 rdl '{\"rdl\": {\"cluster\": \"fake_rdl_string\"}}' &&
    flux kvs get --json $(flux wreck kvs-path 1).rdl > output.12.1 &&
    cat > expected.12.1 <<-EOF &&
{\"cluster\": \"fake_rdl_string\"}
EOF
    test_cmp expected.12.1 output.12.1 
"

test_expect_success 'jstat 13.2: update r_lite' "
    flux jstat update 1 R_lite '{\"R_lite\": [{\"children\": {\"core\": \"0\"}, \"rank\": 0}]}' &&
    flux kvs get --json $(flux wreck kvs-path 1).R_lite > output.13.2 &&
    cat > expected.13.2 <<-EOF &&
[{\"children\": {\"core\": \"0\"}, \"rank\": 0}]
EOF
    test_cmp expected.13.2 output.13.2
"

test_expect_success 'jstat 14: update detects bad inputs' "
    test_expect_code 42 flux jstat update 1 jobid '{\"jobid\": 1}' &&
    test_expect_code 42 flux jstat update 0 rdesc '{\"rdesc\": {\"nnodes\": 128, \"ntasks\": 128,  \"ncores\":128, \"walltime\": 1800}}' &&
    test_expect_code 42 flux jstat update 1 rdesctypo '{\"rdesc\": {\"nnodes\": 128, \"ntasks\": 128, \"ncores\":128, \"walltime\": 3600}}' &&
    test_expect_code 42 flux jstat update 1 rdesc '{\"pdesc\": {\"nnodes\": 128, \"ntasks\": 128,\"ncores\":128, \"walltime\": 2700}}' &&
    test_expect_code 42 flux jstat update 1 state-pair '{\"unknown\": {\"ostate\": 12, \"nstate\": 11}}'
"

test_expect_success 'jstat 15: jstat detects failed state' '
    p=$(run_flux_jstat 15) &&
    test_must_fail run_timeout 4 flux wreckrun -i /bad/input -n4 -N4 hostname &&
    cat >expected15 <<-EOF &&
	null->reserved
	reserved->starting
	starting->failed
	EOF
    wait_until_pattern 15 failed 1 &&
    cp output.15 output.15.cp &&
    kill -INT $p &&
    test_cmp expected15 output.15.cp
'

test_done
