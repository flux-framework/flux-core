#!/bin/sh

test_description='Test flux uptime command'

. $(dirname $0)/sharness.sh

test_under_flux 4

runas_guest() {
        local userid=$(($(id -u)+1))
        FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

test_expect_success 'flux-uptime works on rank 0' '
	flux uptime
'
test_expect_success 'flux-uptime works on rank 1' '
	flux exec -r 1 flux uptime
'
test_expect_success 'flux-uptime works as guest' '
	runas_guest flux uptime
'
test_expect_success 'flux-uptime reports correct size' '
	flux uptime | grep "size 4"
'
test_expect_success 'flux-uptime reports submit disabled' '
	flux queue disable -m "testing" &&
	flux uptime | grep "submit disabled"
'
test_expect_success 'flux-uptime reports scheduler stopped' '
	flux queue stop &&
	flux uptime | grep "scheduler stopped"
'
test_expect_success 'flux-uptime reports drained node count' '
	flux resource drain 1,3 &&
	flux uptime | grep "2 drained"
'

test_done
