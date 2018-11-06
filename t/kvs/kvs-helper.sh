#!/bin/sh
#

# KVS helper functions

# Loop on KVS_WAIT_ITERS is just to make sure we don't spin forever on
# error.

KVS_WAIT_ITERS=50

loophandlereturn() {
    index=$1
    if [ "$index" -eq "${KVS_WAIT_ITERS}" ]
    then
        return 1
    fi
    return 0
}

# arg1 - key to retrieve
# arg2 - expected value
test_kvs_key() {
	flux kvs get --json "$1" >output
	echo "$2" >expected
	test_cmp expected output
}

# arg1 - namespace
# arg2 - key to retrieve
# arg3 - expected value
test_kvs_key_namespace() {
	flux kvs --namespace="$1" get --json "$2" >output
	echo "$3" >expected
	test_cmp expected output
}

# arg1 - namespace
wait_watcherscount_nonzero() {
        ns=$1
        i=0
        while [ "$(flux module stats --parse namespaces.${ns}.watchers kvs-watch 2> /dev/null)" = "0" ] \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}
