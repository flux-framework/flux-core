#!/bin/sh

test_description='Test resource monitoring'

. `dirname $0`/sharness.sh

# min SIZE=4
SIZE=$(test_size_large)
test_under_flux $SIZE

# Usage: waitup N
#   where N is a count of online ranks
waitup () {
	run_timeout 5 flux python -c "import flux; print(flux.Flux().rpc(\"resource.monitor-waitup\",{\"up\":$1}).get())"
}
waitdown () {
	waitup $(($SIZE-$1))
}

test_expect_success 'wait for monitor to declare all ranks are up' '
	waitdown 0
'

test_expect_success 'reload resource with monitor-force-up results in 0 down' '
	flux module remove sched-simple &&
	flux module reload resource monitor-force-up &&
	flux module load sched-simple &&
	waitdown 0
'

test_expect_success 'scheduler thinks no nodes are down' '
	test $(flux resource list -n -s down -o {nnodes}) -eq 0
'

test_done
