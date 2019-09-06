#!/bin/bash

echo before

# to prevent racyness, write something to eventlog to indicate we've
# output foo
flux kvs eventlog append exec.eventlog output

sleep inf

echo after
