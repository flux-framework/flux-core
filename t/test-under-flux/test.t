#!/bin/sh
pwd
test_description="test_under_flux (in sub sharness)"
. "$SHARNESS_TEST_SRCDIR"/sharness.sh
test_under_flux 2 minimal
test_expect_success "flux comms info" "
flux comms info
"
test_done
