#!/usr/bin/env bash

echo before

# to prevent racyness, write something to eventlog to indicate we've
# output foo
flux kvs eventlog append exec.eventlog test-output-ready

sleep inf

echo after
