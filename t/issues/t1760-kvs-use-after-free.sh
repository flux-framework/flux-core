#!/bin/sh -e
# dropcache, do a put that misses a references, unlink a dir, the reference
# should not be missing.

TEST=issue1760
${FLUX_BUILD_DIR}/t/kvs/issue1760 a
