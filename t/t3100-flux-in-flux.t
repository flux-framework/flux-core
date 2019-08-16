#!/bin/sh
#

test_description='Test that Flux can launch Flux'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

test_expect_success "flux can run flux instance as a job" '
	run_timeout 5 flux srun -n1 -N1 \
		flux start flux getattr size >size.out &&
	echo 1 >size.exp &&
	test_cmp size.exp size.out
'

test_expect_success "flux subinstance leaves local_uri, remote_uri in KVS" '
	flux jobspec srun -n1 -N1 flux start /bin/true >j &&
	id=$(flux job submit j) &&
	flux job wait-event $id finish &&
	flux job info $id guest.flux.local_uri &&
	flux job info $id guest.flux.remote_uri
'

test_done
