#!/bin/sh
test_description="test_under_flux with module loaded, but not unloaded"
. "$SHARNESS_TEST_SRCDIR"/sharness.sh
test_under_flux 2 minimal
test_expect_success "flux module load heartbeat" "
flux module load heartbeat
"
test_done
