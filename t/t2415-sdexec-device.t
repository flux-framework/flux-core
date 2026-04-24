#!/bin/sh
# ci=system
test_description='Test imp_exec_helper device containment with systemd'

. $(dirname $0)/sharness.sh

if ! flux version | grep systemd; then
	skip_all="flux was not built with systemd"
	test_done
fi
if ! systemctl --user show --property Version; then
	skip_all="user systemd is not running"
	test_done
fi
if ! busctl --user status >/dev/null; then
	skip_all="user dbus is not running"
	test_done
fi

test_under_flux 1 minimal -Slog-stderr-level=1

sdexec="flux exec --service sdexec"
# Capture full path: sdexec units may not inherit the build-tree PATH
flux_cmd=$(command -v flux)
# Use --test-nojob to skip J lookup, so we can test device options in isolation
imp_helper="$flux_cmd imp_exec_helper --test-nojob"

test_expect_success 'load sdbus,sdexec modules' '
	flux exec flux module load sdbus &&
	flux exec flux module load sdexec
'

#
# DevicePolicy=auto, DeviceAllow empty (default): options present, IMP decides
#
test_expect_success 'auto policy with no DeviceAllow: options present with empty allow' '
	$sdexec -r 0 $imp_helper >auto-empty.json &&
	jq -e ".options.DevicePolicy == \"auto\"" auto-empty.json &&
	jq -e ".options.DeviceAllow | length == 0" auto-empty.json
'

#
# DevicePolicy=closed, DeviceAllow empty: options present with empty allow list
#
test_expect_success 'closed policy: options.DevicePolicy is closed' '
	$sdexec -r 0 \
	    --setopt=SDEXEC_PROP_DevicePolicy=closed \
	    $imp_helper >closed-empty.json &&
	jq -e ".options.DevicePolicy == \"closed\"" closed-empty.json
'
test_expect_success 'closed policy: options.DeviceAllow is empty array' '
	jq -e ".options.DeviceAllow | length == 0" closed-empty.json
'

#
# DevicePolicy=strict, explicit DeviceAllow: raw [specifier, access] tuple passed through
#
test_expect_success 'strict policy with DeviceAllow passes through raw entry' '
	$sdexec -r 0 \
	    --setopt=SDEXEC_PROP_DevicePolicy=strict \
	    --setopt="SDEXEC_PROP_DeviceAllow=/dev/null rw" \
	    $imp_helper >strict.json &&
	jq -e ".options.DeviceAllow | length == 1" strict.json
'
test_expect_success 'strict policy entry is raw [specifier, access] tuple' '
	jq -e ".options.DeviceAllow[0][0] == \"/dev/null\" and
	    .options.DeviceAllow[0][1] == \"rw\"" strict.json
'

#
# DevicePolicy=auto, non-empty DeviceAllow: only the listed entry, no standard devices
#
test_expect_success 'auto policy with non-empty DeviceAllow passes through raw entry' '
	$sdexec -r 0 \
	    --setopt="SDEXEC_PROP_DeviceAllow=/dev/null rw" \
	    $imp_helper >auto-nonempty.json &&
	jq -e ".options.DeviceAllow | length == 1" auto-nonempty.json
'

#
# char-SUBSYSTEM specifier: passed through verbatim, IMP resolves /proc/devices
#
test_expect_success 'strict policy with char-pts passes through raw specifier' '
	$sdexec -r 0 \
	    --setopt=SDEXEC_PROP_DevicePolicy=strict \
	    --setopt="SDEXEC_PROP_DeviceAllow=char-pts rw" \
	    $imp_helper >char-pts.json &&
	jq -e ".options.DeviceAllow | length == 1" char-pts.json &&
	jq -e ".options.DeviceAllow[0][0] == \"char-pts\" and
	    .options.DeviceAllow[0][1] == \"rw\"" char-pts.json
'

test_expect_success 'remove sdexec,sdbus modules' '
	flux exec flux module remove sdexec &&
	flux exec flux module remove sdbus
'

test_done
