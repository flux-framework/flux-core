#!/bin/sh -e
# submit a job with json-null and ensure it doesn't cause the shell
#  to segfault

flux job attach -vEX $(flux run --dry-run hostname \
    | jq .attributes.system.shell.options.foo=null \
    | flux job submit)
