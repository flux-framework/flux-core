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
	run_timeout 25 flux ping --pad 1024 --count 10240 --interval 0 0
'

test_expect_success 'ping: 1K 10K byte echo requests' '
	run_timeout 15 flux ping --pad 10240 --count 1024 --interval 0 0
'

test_expect_success 'ping: 100 100K byte echo requests' '
	run_timeout 15 flux ping --pad 102400 --count 100 --interval 0 0
'

test_expect_success 'ping: 10 1M byte echo requests' '
	run_timeout 15 flux ping --pad 1048576 --count 10 --interval 0 0
'

test_expect_success 'ping: 10 1M byte echo requests (batched)' '
	run_timeout 15 flux ping --pad 1048576 --count 10 --batch --interval 0 0
'

test_expect_success 'ping: 1K 10K byte echo requests (batched)' '
	run_timeout 20 flux ping --pad 10240 --count 1024 --batch --interval 0 0
'

test_expect_success 'ping --rank 1 works' '
	run_timeout 15 flux ping --rank 1 --count 10 --interval 0 broker
'

test_expect_success 'ping 1 works' '
	run_timeout 15 flux ping --count 10 --interval 0 1
'

test_expect_success 'ping 1!broker works' '
	run_timeout 15 flux ping --count 10 --interval 0 "1!broker"
'

test_expect_success 'ping fails on invalid rank (specified as target)' '
	test_must_fail run_timeout 15 flux ping --count 1 $(invalid_rank) 2>stderr &&
	grep -q "No route to host" stderr
'

test_expect_success 'ping fails on invalid rank (specified in option)' '
	test_must_fail run_timeout 15 flux ping --count 1 --rank $(invalid_rank) broker 2>stderr &&
	grep -q "No route to host" stderr
'

test_expect_success 'ping fails on invalid target' '
	test_must_fail run_timeout 15 flux ping --count 1 --rank 0 nosuchtarget 2>stderr &&
	grep -q "Function not implemented" stderr
'

test_expect_success 'ping output format for "any" rank is correct (default)' '
	run_timeout 15 flux ping --count 1 broker 1>stdout &&
        grep -q "^broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for "any" rank is correct (format 1)' '
	run_timeout 15 flux ping --count 1 --rank any broker 1>stdout &&
        grep -q "^broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for "any" rank is correct (format 2)' '
	run_timeout 15 flux ping --count 1 any!broker 1>stdout &&
        grep -q "^broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for "any" rank is correct (format 3)' '
	run_timeout 15 flux ping --count 1 any 1>stdout &&
        grep -q "^broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific rank is correct (format 1)' '
	run_timeout 15 flux ping --count 1 --rank 0 broker 1>stdout &&
        grep -q "^0!broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific rank is correct (format 2)' '
	run_timeout 15 flux ping --count 1 0!broker 1>stdout &&
        grep -q "^0!broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping output format for specific rank is correct (format 3)' '
	run_timeout 15 flux ping --count 1 0 1>stdout &&
        grep -q "^0!broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

# test "upstream" via exec.  Ping started on rank 0 should result in
# an error b/c there is no where to go upstream.  Ping executed on
# rank 1 should work

test_expect_success 'ping with "upstream" fails on rank 0' '
        test_must_fail run_timeout 15 flux exec -n --rank 0 flux ping --count 1 --rank upstream broker 2>stderr &&
	grep -q "No route to host" stderr
'

test_expect_success 'ping with "upstream" works (format 1)' '
        run_timeout 15 flux exec -n --rank 1 flux ping --count 1 --rank upstream broker 1>stdout &&
        grep -q "^upstream!broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping with "upstream" works (format 2)' '
        run_timeout 15 flux exec -n --rank 1 flux ping --count 1 upstream!broker 1>stdout &&
        grep -q "^upstream!broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping with "upstream" works (format 3)' '
        run_timeout 15 flux exec -n --rank 1 flux ping --count 1 upstream 1>stdout &&
        grep -q "^upstream!broker.ping" stdout &&
        grep -q -E "time=[0-9]+\.[0-9]+ ms" stdout
'

test_expect_success 'ping help output works' '
        flux ping --help 2> help.err &&
        grep "Usage: flux-ping \[OPTIONS\] TARGET" help.err
'

test_expect_success 'ping works with hostname' '
        flux ping --count=1 $(hostname -s)
'
test_expect_success 'ping works with hostname!service' '
        flux ping --count=1 "$(hostname -s)!broker"
'
test_expect_success 'ping fails with unknown hostname' '
        test_must_fail flux ping --count=1 "notmyhost!broker"
'

test_done
