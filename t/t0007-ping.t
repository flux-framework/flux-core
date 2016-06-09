#!/bin/sh
#

test_description='Stress test the local connector with flood pings
'

. `dirname $0`/sharness.sh
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal

test_expect_success 'ping: 10K 1K byte echo requests' '
	run_timeout 5 flux ping --pad 1024 --count 10240 --delay 0 0
'

test_expect_success 'ping: 1K 10K byte echo requests' '
	run_timeout 5 flux ping --pad 10240 --count 1024 --delay 0 0
'

test_expect_success 'ping: 100 100K byte echo requests' '
	run_timeout 5 flux ping --pad 102400 --count 100 --delay 0 0
'

test_expect_success 'ping: 10 1M byte echo requests' '
	run_timeout 5 flux ping --pad 1048576 --count 10 --delay 0 0
'

test_expect_success 'ping: 10 1M byte echo requests (batched)' '
	run_timeout 5 flux ping --pad 1048576 --count 10 --batch --delay 0 0
'

test_expect_success 'ping: 1K 10K byte echo requests (batched)' '
	run_timeout 5 flux ping --pad 10240 --count 1024 --batch --delay 0 0
'

test_expect_success 'ping --rank 1 works' '
	run_timeout 5 flux ping --rank 1 --count 10 --delay 0 cmb
'

test_expect_success 'ping 1 works' '
	run_timeout 5 flux ping --count 10 --delay 0 1
'

test_expect_success 'ping 1!cmb works' '
	run_timeout 5 flux ping --count 10 --delay 0 "1!cmb"
'

test_expect_success 'ping --rank all works' '
	run_timeout 5 flux ping --rank all --count 10 --delay 0 cmb
'

test_expect_success 'ping all works' '
	run_timeout 5 flux ping --count 10 --delay 0 all
'

test_expect_success 'ping all works with 64K payload' '
	run_timeout 5 flux ping --pad 65536 --count 10 --delay 0 all
'

test_done
