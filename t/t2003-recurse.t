#!/bin/sh
#

test_description='Test that Flux can launch Flux'

. `dirname $0`/sharness.sh

mock_bootstrap_instance
test_under_flux 4

test_expect_success 'recurse: Flux launches Flux ' '
	printenv FLUX_URI >old_uri &&
	test -s old_uri &&
	flux wreckrun -n1 -N1 flux start \
		printenv FLUX_URI >new_uri &&
	test -s new_uri &&
	! test_cmp old_uri new_uri
'

test_expect_success 'recurse: local-uri is identical to FLUX_URI' '
	printenv FLUX_URI >cur_uri &&
	test -s cur_uri &&
	flux getattr local-uri >attr_uri &&
	test -s attr_uri &&
	test_cmp cur_uri attr_uri
'

test_expect_success 'recurse: parent-uri is unset in bootstrap instance' '
	! flux getattr parent-uri
'

test_expect_success 'recurse: local-uri != local-uri in enclosing instance' '
	flux getattr local-uri >enc_uri &&
	test -s enc_uri &&
	flux wreckrun -n1 -N1 flux start \
		flux getattr local-uri >attr_uri &&
	test -s attr_uri &&
	! test_cmp enc_uri attr_uri
'

test_expect_success 'recurse: parent-uri == local-uri in enclosing instance' '
	flux getattr local-uri >enc_uri &&
	test -s enc_uri &&
	flux wreckrun -n1 -N1 flux start \
		flux getattr parent-uri >attr_uri &&
	test -s attr_uri &&
	test_cmp enc_uri attr_uri
'

test_expect_success 'recurse: Flux launches Flux launches Flux' '
	flux wreckrun -n1 -N1 flux start \
		flux wreckrun -n1 -N1 flux start \
			echo hello >hello_out &&
	echo hello >hello_expected &&
	test_cmp hello_expected hello_out
'

test_expect_success 'recurse: FLUX_JOB_KVSPATH is set in child job' '
	flux wreckrun -n1 -N1 flux start \
		printenv FLUX_JOB_KVSPATH >kvspath &&
	test -s kvspath
'

test_expect_success 'recurse: flux.local_uri is set in enclosing KVS' '
	flux wreckrun -n1 -N1 flux start flux getattr local-uri >curi &&
	key=$(flux wreck kvs-path $(flux wreck last-jobid)).flux.local_uri &&
	flux kvs get --json $key >curi.out &&
	test_cmp curi curi.out
'

test_expect_success 'recurse: flux.remote_uri is set in enclosing KVS' '
	flux wreckrun -n1 -N1 flux start flux getattr local-uri >curi2 &&
	key=$(flux wreck kvs-path $(flux wreck last-jobid)).flux.remote_uri &&
	flux kvs get --json $key >ruri.out &&
	grep -q "$(sed -e ,local://,, <curi2)" ruri.out
'

test_expect_success 'recurse: flux wreck uri works for child job' '
	flux wreckrun -n1 -N1 flux start flux getattr local-uri >child_uri &&
	flux wreck uri --bare $(flux wreck last-jobid) >list_uri &&
	grep -q "$(sed -e ,local://,, <child_uri)" list_uri
'

test_done
