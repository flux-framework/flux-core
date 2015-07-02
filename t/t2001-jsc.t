#!/bin/sh

test_description='Test basic jstat functionality

Test the basic functionality of job status and control API.'

. `dirname $0`/sharness.sh

test_under_flux 4
if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

tr1="null->null"
tr2="null->reserved"
tr3="reserved->starting"
tr4="starting->running"
tr5="running->complete"
trans="$tr1
$tr2
$tr3
$tr4
$tr5"

run_flux_jstat () {
    sess=$1
    rm -f jstat$sess.pid
    (
        # run this in a subshell
        flux jstat -o output.$sess notify & 
        p=$!
        cat <<HEREDOC > jstat$sess.pid
$p
HEREDOC
        wait $p
        #rm -f output.$sess
    )&
    return 0
}

sync_flux_jstat () {
    sess=$1
    while [ ! -f output.$sess ]
    do
        sleep 2
    done
    p=`cat jstat$sess.pid`
    echo $p
}

overlap_flux_wreckruns () {
    insts=$(($1-1))
    pids=""
    for i in `seq 0 $insts`; do
        st=$(($insts + 3 - 2*$i))
        flux wreckrun -n4 -N4 sleep $st  &
        pids="$pids $!"
    done
    for i in $pids; do 
        wait $i
    done
    return 0
}

test_expect_success RACY 'jstat 1: notification works for 1 wreckrun' '
    run_flux_jstat 1 &&
    p=$( sync_flux_jstat 1) &&
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&
    cat >expected1 <<-EOF &&
$trans
EOF
    cp output.1 output.1.cp &&
    kill -INT $p &&
    test_cmp expected1 output.1.cp 
'

test_expect_success RACY 'jstat 2: jstat back-to-back works' '
    run_flux_jstat 2 &&
    p=$( sync_flux_jstat 2) &&
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&
    cat >expected2 <<-EOF &&
$trans
EOF
    cp output.2 output.2.cp &&
    kill -INT $p &&
    test_cmp expected2 output.2.cp 
'

test_expect_success RACY 'jstat 3: notification works for multiple wreckruns' '
    run_flux_jstat 3 &&
    p=$( sync_flux_jstat 3 ) &&
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&
	run_timeout 4 flux wreckrun -n4 -N4 hostname &&
	run_timeout 4 flux wreckrun -n4 -N4 hostname &&
    cat >expected3 <<-EOF &&
$trans
$trans
$trans
EOF
    cp output.3 output.3.cp &&
    kill -INT $p &&
    test_cmp expected3 output.3.cp 
'

test_expect_success RACY,LONGTEST 'jstat 4: notification works under lock-step stress' '
    run_flux_jstat 4 &&
    p=$( sync_flux_jstat 4 ) &&
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
    cp output.4 output.4.cp &&
    kill -INT $p &&
    test_cmp expected4 output.4.cp 
'

test_expect_success RACY 'jstat 5: notification works for overlapping wreckruns' '
    run_flux_jstat 5 &&
    p=$( sync_flux_jstat 5 ) &&
    overlap_flux_wreckruns 3 &&
    cat >expected5 <<-EOF &&
$trans
$trans
$trans
EOF
    sort expected5 > expected5.sort &&
    cp output.5 output.5.cp &&
    sort output.5.cp > output.5.sort &&
    kill -INT $p &&
    test_cmp expected5.sort output.5.sort
'

test_expect_success RACY,LONGTEST 'jstat 6: notification works for overlapping stress' '
    run_flux_jstat 6 &&
    p=$( sync_flux_jstat 6 ) &&
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
    cp output.6 output.6.cp &&
    sort output.6.cp > output.6.sort &&
    kill -INT $p &&
    test_cmp expected6.sort output.6.sort
'

test_expect_success 'jstat 7: basic query works' '
    run_timeout 4 flux wreckrun -n4 -N4 hostname &&  # create some state for
    flux jstat query 1 jobid &&                      #   subsequent tests
    flux jstat query 1 state-pair &&                 #   in case RACY not set
    flux jstat query 1 rdesc &&
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
    flux kvs get lwj.1.state > output.9.1 &&
    cat >expected.9.1 <<-EOF &&
cancelled
EOF
    test_cmp expected.9.1 output.9.1 
"

test_expect_success 'jstat 10: update procdescs' "
    flux kvs get lwj.1.0.procdesc > output.10.1
    flux jstat update 1 pdesc '{\"pdesc\": {\"procsize\":1, \"hostnames\":[\"0\"], \"executables\":[\"fake\"], \"pdarray\":[{\"pid\":8482,\"eindx\":0,\"hindx\":0}]}}' &&
    flux kvs get lwj.1.0.procdesc > output.10.2 &&
    test_expect_code 1 diff output.10.1 output.10.2 
"

test_expect_success 'jstat 11: update rdesc' "
    flux jstat update 1 rdesc '{\"rdesc\": {\"nnodes\": 128, \"ntasks\": 128}}' &&
    flux kvs get lwj.1.ntasks > output.11.1 &&
    cat > expected.11.1 <<-EOF &&
128
EOF
    test_cmp expected.11.1 output.11.1 
"

test_expect_success 'jstat 12: update rdl' "
    flux jstat update 1 rdl '{\"rdl\": \"fake_rdl_string\"}' &&
    flux kvs get lwj.1.rdl > output.12.1 &&
    cat > expected.12.1 <<-EOF &&
fake_rdl_string
EOF
    test_cmp expected.12.1 output.12.1 
"

test_expect_success 'jstat 13: update rdl_alloc' "
    flux jstat update 1 rdl_alloc '{\"rdl_alloc\": [{\"contained\": {\"cmbdncores\": 102}}]}' &&
    flux kvs get lwj.1.rank.0.cores > output.13.1 &&
    cat > expected.13.1 <<-EOF &&
102
EOF
    test_cmp expected.13.1 output.13.1 
"

test_expect_success 'jstat 14: update detects bad inputs' "
    test_expect_code 42 flux jstat update 1 jobid '{\"jobid\": 1}' &&
    test_expect_code 42 flux jstat update 0 rdesc '{\"rdesc\": {\"nnodes\": 128, \"ntasks\": 128}}' &&
    test_expect_code 42 flux jstat update 1 rdesctypo '{\"rdesc\": {\"nnodes\": 128, \"ntasks\": 128}}' &&
    test_expect_code 42 flux jstat update 1 rdesc '{\"pdesc\": {\"nnodes\": 128, \"ntasks\": 128}}' &&
    test_expect_code 42 flux jstat update 1 state-pair '{\"unknown\": {\"ostate\": 12, \"nstate\": 11}}'
"

test_done
