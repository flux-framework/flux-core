#!/bin/sh -e
# kvs put x=foo, get x.y fails without panic

TEST=issue441

flux kvs put --json ${TEST}.x=foo

flux kvs get ${TEST}.x.y && test $? -eq 1

flux kvs get ${TEST}.x   # fails if broker died
