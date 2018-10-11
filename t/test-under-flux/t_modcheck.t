#!/bin/sh
test_description="test_under_flux with module loaded, but not unloaded"
. "$SHARNESS_TEST_SRCDIR"/sharness.sh
test_under_flux 2 minimal
test_expect_success "flux module load kvs" "
flux module load -r 0 kvs
"
test_done
