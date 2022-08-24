#!/bin/sh
#

# content module test helper functions

# checkpoint rootref at specific key
# Usage: checkpoint_put key rootref
checkpoint_put() {
	o="{key:\"$1\",value:{version:1,rootref:\"$2\",timestamp:2.2}}"
	jq -j -c -n  ${o} | $RPC content.checkpoint-put
}

# get checkpoint rootref at key
# Usage: checkpoint_get key
checkpoint_get() {
	jq -j -c -n  "{key:\"$1\"}" | $RPC content.checkpoint-get
}
