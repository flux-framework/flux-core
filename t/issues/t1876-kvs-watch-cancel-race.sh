#!/bin/sh -e

# initial call to getroot() can race with an early call to
# flux_kvs_lookup_cancel(), leading to a namespace data
# structure being destroyed then used afterwards.

TEST=issue1876
${FLUX_BUILD_DIR}/t/kvs/issue1876 test.a
