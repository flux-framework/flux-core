#!/bin/sh -e
# flux kvs namespace -h
# and
# flux kvs eventlog -h
# don't output appropriate help output

TEST=issue3492

flux kvs namespace -h 2>&1 | grep create && test $? -eq 0
flux kvs eventlog -h 2>&1 | grep append && test $? -eq 0
