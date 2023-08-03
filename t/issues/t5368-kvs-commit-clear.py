#!/usr/bin/env python3
#
#  Internal KVS txn cleared between commits.
#
import sys
import flux
import flux.kvs
import os
import errno

handle = flux.Flux()

flux.kvs.put(handle, "issue5368A", 1)
flux.kvs.commit(handle)
val = flux.kvs.get(handle, "issue5368A")
if val != 1:
    print("could not write issue5368A")
    sys.exit(1)

# delete key outside of this test's flux handle
os.system("flux kvs unlink issue5368A")

flux.kvs.put(handle, "issue5368B", 1)
flux.kvs.commit(handle)

# If the internal KVS transaction is not cleared, then the key
# "issue5368A" will be rewritten.  We want ENOENT to occur.
try:
    val = flux.kvs.get(handle, "issue5368A")
    print("key issue5368A retrieved successfully")
except OSError as e:
    if e.errno == errno.ENOENT:
        sys.exit(0)
    print("unexpected errno " + e.errno)

sys.exit(1)
