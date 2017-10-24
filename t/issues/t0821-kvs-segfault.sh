#!/bin/sh -e
# kvs put test="large string", get test.x fails without panic

TEST=issue0821
flux kvs put --json ${TEST}="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
flux kvs get ${TEST}.x && test $? -eq 1
flux kvs get ${TEST}   # fails if broker died
