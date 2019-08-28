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

test_expect_success "flux --parent works in subinstance" '
	id=$(flux jobspec srun -n1 \
               flux start flux --parent kvs put test=ok \
               | flux job submit) &&
	flux job attach $id &&
	flux job info $id guest.test > guest.test &&
	cat <<-EOF >guest.test.exp &&
	ok
	EOF
	test_cmp guest.test.exp guest.test
'

test_expect_success "flux --parent --parent works in subinstance" '
	id=$(flux jobspec srun -n1 \
              flux start flux start flux --parent --parent kvs put test=ok \
	      | flux job submit) &&
	flux job attach $id &&
	flux job info $id guest.test > guest2.test &&
	cat <<-EOF >guest2.test.exp &&
	ok
	EOF
	test_cmp guest2.test.exp guest2.test
'

test_expect_success "flux sets instance-level attribute" '
        level=$(flux srun flux start \
                flux getattr instance-level) &&
        level2=$(flux srun flux start \
                 flux srun flux start \
                 flux getattr instance-level) &&
	level0=$(flux start flux getattr instance-level) &&
        test "$level" = "1" &&
        test "$level2" = "2" &&
        test "$level0" = "0"
 '

test_done
