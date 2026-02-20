#!/bin/sh
#

# KVS helper functions

# Loop on KVS_WAIT_ITERS is just to make sure we don't spin forever on
# error.

KVS_WAIT_ITERS=50

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
	local i=0
	while [ $i -lt ${KVS_WAIT_ITERS} ]
	do
	    if flux module stats --parse namespaces.${ns}.watchers kvs-watch > /dev/null 2>&1 \
	       && [ "$(flux module stats --parse namespaces.${ns}.watchers kvs-watch 2> /dev/null)" != "0" ]
	    then
		return 0
	    fi
	    sleep 0.1
	    i=$((i + 1))
	done
	return 1
}
