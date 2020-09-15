#!/bin/sh
pwd
test_description="test_under_flux (in sub sharness)"
. "$SHARNESS_TEST_SRCDIR"/sharness.sh
test_under_flux 2 minimal
test_expect_success "flux getattr size" "
flux getattr size
"
test_done
