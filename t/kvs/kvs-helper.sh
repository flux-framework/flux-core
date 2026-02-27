#!/bin/sh
#

# KVS helper functions

# arg1 - key to retrieve
# arg2 - expected value
test_kvs_key() {
	flux kvs get "$1" >output
	echo "$2" >expected
	test_cmp expected output
}

# arg1 - namespace
# arg2 - key to retrieve
# arg3 - expected value
test_kvs_key_namespace() {
	flux kvs get --namespace="$1" "$2" >output
	echo "$3" >expected
	test_cmp expected output
}

# arg1 - namespace
wait_watcherscount_nonzero() {
	ns=$1
	wait_util "flux module stats --parse namespaces.${ns}.watchers kvs-watch > /dev/null 2>&1 \
		&& [ \"\$(flux module stats --parse namespaces.${ns}.watchers kvs-watch 2> /dev/null)\" != \"0\" ]"
}
