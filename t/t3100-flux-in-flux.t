#!/bin/sh
#

test_description='Test that Flux can launch Flux'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

ARGS="-o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,--shutdown-grace=0.25"
test_expect_success "flux can run flux instance as a job" '
	run_timeout 10 flux mini run -n1 -N1 \
		flux start ${ARGS} flux getattr size >size.out &&
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
               flux start ${ARGS} flux --parent kvs put test=ok \
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
              flux start ${ARGS} \
	      flux start ${ARGS} flux --parent --parent kvs put test=ok \
	      | flux job submit) &&
	flux job attach $id &&
	flux job info $id guest.test > guest2.test &&
	cat <<-EOF >guest2.test.exp &&
	ok
	EOF
	test_cmp guest2.test.exp guest2.test
'

test_expect_success "instance-level attribute = 0 in new stanalone instance" '
	flux start ${ARGS} flux getattr instance-level >level_new.out &&
	echo 0 >level_new.exp &&
	test_cmp level_new.exp level_new.out
'

test_expect_success "instance-level attribute = 0 in test instance" '
	flux getattr instance-level >level0.out &&
	echo 0 >level0.exp &&
	test_cmp level0.exp level0.out
'

test_expect_success "instance-level attribute = 1 in first subinstance" '
        flux mini run flux start ${ARGS} \
            flux getattr instance-level >level1.out &&
	echo 1 >level1.exp &&
	test_cmp level1.exp level1.out
'

test_expect_success "instance-level attribute = 2 in second subinstance" '
        flux mini run flux start \
	    flux mini run flux start ${ARGS} \
		flux getattr instance-level >level2.out &&
	echo 2 >level2.exp &&
	test_cmp level2.exp level2.out
'

test_expect_success "flux sets jobid attribute" '
	id=$(flux jobspec srun -n1 \
		flux start ${ARGS} flux getattr jobid \
		| flux job submit) &&
	echo "$id" >jobid.exp &&
	flux job attach $id >jobid.out &&
	test_cmp jobid.exp jobid.out
'

test_done
