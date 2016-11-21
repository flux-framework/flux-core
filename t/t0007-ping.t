#!/bin/sh
#

test_description='Stress test the local connector with flood pings
'

. `dirname $0`/sharness.sh
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal

invalid_rank() {
	echo $((${SIZE} + 1))
}

test_expect_success 'ping: 10K 1K byte echo requests' '
	run_timeout 5 flux ping --pad 1024 --count 10240 --interval 0 0
'

test_expect_success 'ping: 1K 10K byte echo requests' '
	run_timeout 5 flux ping --pad 10240 --count 1024 --interval 0 0
'

test_expect_success 'ping: 100 100K byte echo requests' '
	run_timeout 5 flux ping --pad 102400 --count 100 --interval 0 0
'

test_expect_success 'ping: 10 1M byte echo requests' '
	run_timeout 5 flux ping --pad 1048576 --count 10 --interval 0 0
'

test_expect_success 'ping: 10 1M byte echo requests (batched)' '
	run_timeout 5 flux ping --pad 1048576 --count 10 --batch --interval 0 0
'

test_expect_success 'ping: 1K 10K byte echo requests (batched)' '
	run_timeout 5 flux ping --pad 10240 --count 1024 --batch --interval 0 0
'

test_expect_success 'ping --rank 1 works' '
	run_timeout 5 flux ping --rank 1 --count 10 --interval 0 cmb
'

test_expect_success 'ping 1 works' '
	run_timeout 5 flux ping --count 10 --interval 0 1
'

test_expect_success 'ping 1!cmb works' '
	run_timeout 5 flux ping --count 10 --interval 0 "1!cmb"
'

test_expect_success 'ping --rank all works' '
	run_timeout 5 flux ping --rank all --count 10 --interval 0 cmb
'

test_expect_success 'ping all works' '
	run_timeout 5 flux ping --count 10 --interval 0 all
'

test_expect_success 'ping all works with 64K payload' '
	run_timeout 5 flux ping --pad 65536 --count 10 --interval 0 all
'

test_expect_success 'ping fails on invalid rank (specified as target)' '
	run_timeout 5 flux ping --count 1 $(invalid_rank) 2>stderr
	grep -q "No route to host" stderr
'

test_expect_success 'ping fails on invalid rank (specified in option)' '
	run_timeout 5 flux ping --count 1 --rank $(invalid_rank) cmb 2>stderr
	grep -q "No route to host" stderr
'

test_expect_success 'ping works on valid and invalid rank' '
	run_timeout 5 flux ping --count 1 --rank 0,$(invalid_rank) cmb 1>stdout 2>stderr
	grep -q "No route to host" stderr &&
	grep -q "0,$(invalid_rank)!cmb.ping" stdout
'

test_expect_success 'ping fails on invalid target' '
	run_timeout 5 flux ping --count 1 --rank 0 nosuchtarget 2>stderr
	grep -q "Function not implemented" stderr
'

test_expect_success 'ping output format for "any" single rank is correct (default)' '
	run_timeout 5 flux ping --count 1 cmb 1>stdout
        grep -q "^cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for "any" single rank is correct (format 1)' '
	run_timeout 5 flux ping --count 1 --rank any cmb 1>stdout
        grep -q "^cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for "any" single rank is correct (format 2)' '
	run_timeout 5 flux ping --count 1 any!cmb 1>stdout
        grep -q "^cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for "any" single rank is correct (format 3)' '
	run_timeout 5 flux ping --count 1 any 1>stdout
        grep -q "^cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific single rank is correct (format 1)' '
	run_timeout 5 flux ping --count 1 --rank 0 cmb 1>stdout
        grep -q "^0!cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific single rank is correct (format 2)' '
	run_timeout 5 flux ping --count 1 0!cmb 1>stdout
        grep -q "^0!cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific single rank is correct (format 3)' '
	run_timeout 5 flux ping --count 1 0 1>stdout
        grep -q "^0!cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific multiple ranks is correct (format 1)' '
	run_timeout 5 flux ping --count 1 --rank 0-1 cmb 1>stdout
        grep -q "^0-1!cmb.ping" stdout &&
        grep -q -E "time=\([0-9]+\.[0-9]+:[0-9]+\.[0-9]+:[0-9]+\.[0-9]+\)" stdout
'

test_expect_success 'ping output format for specific multiple ranks is correct (format 2)' '
	run_timeout 5 flux ping --count 1 0-1!cmb 1>stdout
        grep -q "^0-1!cmb.ping" stdout &&
        grep -q -E "time=\([0-9]+\.[0-9]+:[0-9]+\.[0-9]+:[0-9]+\.[0-9]+\)" stdout
'

test_expect_success 'ping output format for specific multiple ranks is correct (format 3)' '
	run_timeout 5 flux ping --count 1 0-1 1>stdout
        grep -q "^0-1!cmb.ping" stdout &&
        grep -q -E "time=\([0-9]+\.[0-9]+:[0-9]+\.[0-9]+:[0-9]+\.[0-9]+\)" stdout
'

test_expect_success 'ping output format for all ranks is correct (format 1)' '
	run_timeout 5 flux ping --count 1 --rank all cmb 1>stdout
        grep -q "^all!cmb.ping" stdout &&
        grep -q -E "time=\([0-9]+\.[0-9]+:[0-9]+\.[0-9]+:[0-9]+\.[0-9]+\)" stdout
'

test_expect_success 'ping output format for all ranks is correct (format 2)' '
	run_timeout 5 flux ping --count 1 all!cmb 1>stdout
        grep -q "^all!cmb.ping" stdout &&
        grep -q -E "time=\([0-9]+\.[0-9]+:[0-9]+\.[0-9]+:[0-9]+\.[0-9]+\)" stdout
'

test_expect_success 'ping output format for all ranks is correct (format 3)' '
	run_timeout 5 flux ping --count 1 all 1>stdout
        grep -q "^all!cmb.ping" stdout &&
        grep -q -E "time=\([0-9]+\.[0-9]+:[0-9]+\.[0-9]+:[0-9]+\.[0-9]+\)" stdout
'

# test "upstream" via exec.  Ping started on rank 0 should result in
# an error b/c there is no where to go upstream.  Ping executed on
# rank 1 should work

test_expect_success 'ping with "upstream" fails on rank 0' '
        run_timeout 5 flux exec --rank 0 flux ping --count 1 --rank upstream cmb 2>stderr
	grep -q "No route to host" stderr
'

test_expect_success 'ping with "upstream" works (format 1)' '
        run_timeout 5 flux exec --rank 1 flux ping --count 1 --rank upstream cmb 1>stdout
        grep -q "^upstream!cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping with "upstream" works (format 2)' '
        run_timeout 5 flux exec --rank 1 flux ping --count 1 upstream!cmb 1>stdout
        grep -q "^upstream!cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping with "upstream" works (format 3)' '
        run_timeout 5 flux exec --rank 1 flux ping --count 1 upstream 1>stdout
        grep -q "^upstream!cmb.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_done
