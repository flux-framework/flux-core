#!/bin/sh
test_description='Test flux queue command'

. $(dirname $0)/sharness.sh

test_under_flux 1

flux setattr log-stderr-level 1

test_expect_success 'flux-queue: unknown sub-command fails with usage message' '
	test_must_fail flux queue wrongsubcmd 2>usage.out &&
	grep Usage: usage.out
'

test_expect_success 'flux-queue: missing sub-command fails with usage message' '
	test_must_fail flux queue 2>usage2.out &&
	grep Usage: usage2.out
'

test_expect_success 'flux-queue: disable with no reason fails' '
	test_must_fail flux queue submit disable 2>usage3.out &&
	grep Usage: usage3.out
'

test_expect_success 'flux-queue: enable with extra free args fails' '
	test_must_fail flux queue enable xyz 2>usage4.out &&
	grep Usage: usage4.out
'

test_expect_success 'flux-queue: status with extra free args fails' '
	test_must_fail flux queue status xyz 2>usage5.out &&
	grep Usage: usage5.out
'

test_expect_success 'flux-queue: status with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue status
'

test_done
