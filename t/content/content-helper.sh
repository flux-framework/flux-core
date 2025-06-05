#!/bin/sh
#

# content module test helper functions

# Get RPC message contents for checkpoint put
# Usage: checkpoint_put_msg rootref
checkpoint_put_msg() {
	o="{value:{version:1,rootref:\"$1\",timestamp:2.2}}"
	echo ${o}
}

# checkpoint rootref
# Usage: checkpoint_put rootref
checkpoint_put() {
	o=$(checkpoint_put_msg $1)
	jq -j -c -n ${o} | $RPC content.checkpoint-put
}

# Get RPC message contents for checkpoint get
# Usage: checkpoint_get_msg
checkpoint_get_msg() {
	o="{}"
	echo ${o}
}

# get checkpoint rootref
# Usage: checkpoint_get
checkpoint_get() {
	o=$(checkpoint_get_msg)
	jq -j -c -n ${o} | $RPC content.checkpoint-get
}

# Identical to checkpoint_put(), but go directly to backing store
# Usage: checkpoint_put rootref
checkpoint_backing_put() {
	o=$(checkpoint_put_msg $1)
	jq -j -c -n ${o} | $RPC content-backing.checkpoint-put
}

# Identical to checkpoint_get(), but go directly to backing store
# Usage: checkpoint_get
checkpoint_backing_get() {
	o=$(checkpoint_get_msg)
	jq -j -c -n ${o} | $RPC content-backing.checkpoint-get
}

# legacy equivalents to above, all take 'key'

# Usage: checkpoint_legacy_put_msg key rootref
checkpoint_legacy_put_msg() {
	o="{key:\"$1\",value:{version:1,rootref:\"$2\",timestamp:2.2}}"
	echo ${o}
}

# Usage: checkpoint_legacy_put key rootref
checkpoint_legacy_put() {
	o=$(checkpoint_legacy_put_msg $1 $2)
	jq -j -c -n ${o} | $RPC content.checkpoint-put
}

# Usage: checkpoint_legacy_get_msg key
checkpoint_legacy_get_msg() {
	o="{key:\"$1\"}"
	echo ${o}
}

# Usage: checkpoint_legacy_get key
checkpoint_legacy_get() {
	o=$(checkpoint_legacy_get_msg $1)
	jq -j -c -n ${o} | $RPC content.checkpoint-get
}

# Usage: checkpoint_legacy_put key rootref
checkpoint_legacy_backing_put() {
	o=$(checkpoint_legacy_put_msg $1 $2)
	jq -j -c -n ${o} | $RPC content-backing.checkpoint-put
}

# Usage: checkpoint_legacy_get key
checkpoint_legacy_backing_get() {
	o=$(checkpoint_legacy_get_msg $1)
	jq -j -c -n ${o} | $RPC content-backing.checkpoint-get
}
