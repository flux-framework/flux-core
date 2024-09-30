#!/bin/bash -e

# create a fake job eventlog in the KVS, no normal mechanism will
# create an empty eventlog

jobpath=`flux job id --to=kvs 123456789`
flux kvs put "${jobpath}.eventlog"=""

# Issue 4413, previously would return Cannot allocate memory
flux job info 123456789 eventlog 2>&1 | grep "error parsing eventlog"


