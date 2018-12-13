#!/bin/sh

test_description='Test KVS getroot'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

test_expect_success 'flux kvs getroot returns valid dirref object' '
	flux kvs put test.a=42 &&
	DIRREF=$(flux kvs getroot) &&
	flux kvs put test.a=43 &&
	flux kvs get --at "$DIRREF" test.a >get.out &&
	echo 42 >get.exp &&
	test_cmp get.exp get.out
'

test_expect_success 'flux kvs getroot --blobref returns valid blobref' '
	BLOBREF=$(flux kvs getroot --blobref) &&
	flux content load $BLOBREF >/dev/null
'

test_expect_success 'flux kvs getroot --sequence returns increasing rootseq' '
	SEQ=$(flux kvs getroot --sequence) &&
	flux kvs put test.b=hello &&
	SEQ2=$(flux kvs getroot --sequence) &&
	test $SEQ -lt $SEQ2
'

test_expect_success 'flux kvs getroot --owner returns instance owner' '
	OWNER=$(flux kvs getroot --owner) &&
	test $OWNER -eq $(id -u)
'

test_expect_success 'flux kvs getroot works on alt namespace' '
	flux kvs namespace-create testns1 &&
	SEQ=$(flux kvs --namespace=testns1 getroot --sequence) &&
	test $SEQ -eq 0 &&
	flux kvs --namespace=testns1 put test.c=moop &&
	SEQ2=$(flux kvs --namespace=testns1 getroot --sequence) &&
	test $SEQ -lt $SEQ2 &&
	flux kvs namespace-remove testns1
'

test_done
