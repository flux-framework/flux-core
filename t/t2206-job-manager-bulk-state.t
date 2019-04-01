#!/bin/sh

test_description='Test flux job manager state notification'

. $(dirname $0)/sharness.sh

test_under_flux 4

flux setattr log-stderr-level 1

BULK_STATE=${FLUX_SOURCE_DIR}/t/job-manager/bulk-state.py

test_expect_success 'job-manager: all state events received (5 jobs from rank 0)' '
	run_timeout 10 ${BULK_STATE} 5
'

test_expect_success 'job-manager: all state events received (2 jobs from 4 ranks)' '
	run_timeout 10 flux exec -r all ${BULK_STATE} 2
'

test_done
