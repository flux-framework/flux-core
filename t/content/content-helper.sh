#!/bin/sh
#

# content module test helper functions

# Get RPC message contents for checkpoint put
# Usage: checkpoint_put_msg key rootref
checkpoint_put_msg() {
	o="{key:\"$1\",value:{version:1,rootref:\"$2\",timestamp:2.2}}"
	echo ${o}
}

# checkpoint rootref at specific key
# Usage: checkpoint_put key rootref
checkpoint_put() {
	o=$(checkpoint_put_msg $1 $2)
	jq -j -c -n ${o} | $RPC content.checkpoint-put
}

# Get RPC message contents for checkpoint get
# Usage: checkpoint_get_msg key
checkpoint_get_msg() {
	o="{key:\"$1\"}"
	echo ${o}
}

# get checkpoint rootref at key
# Usage: checkpoint_get key
checkpoint_get() {
	o=$(checkpoint_get_msg $1)
	jq -j -c -n ${o} | $RPC content.checkpoint-get
}

# Identical to checkpoint_put(), but go directly to backing store
# Usage: checkpoint_put key rootref
checkpoint_backing_put() {
	o=$(checkpoint_put_msg $1 $2)
	jq -j -c -n ${o} | $RPC content-backing.checkpoint-put
}

# Identical to checkpoint_get(), but go directly to backing store
# Usage: checkpoint_get key
checkpoint_backing_get() {
	o=$(checkpoint_get_msg $1)
	jq -j -c -n ${o} | $RPC content-backing.checkpoint-get
}
