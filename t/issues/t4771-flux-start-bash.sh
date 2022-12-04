#!/bin/sh -x
# flux start of interactive process non-interactively causes error

# create link to shell to thwart broker detection of interactive shell:
ln -s /bin/bash bash

${SHARNESS_TEST_SRCDIR}/scripts/runpty.py flux start ./bash

# Ensure broker killed bash with SIGKILL:
test $? -eq 137
