#!/bin/sh
#
# ci=asan

test_description='Test flux admin system-scripts command'

. `dirname $0`/sharness.sh

test_under_flux 2 full

test_expect_success 'flux admin system-scripts command exists' '
	flux admin system-scripts --help
'

test_expect_success 'flux admin system-scripts runs without error' '
	flux admin system-scripts >basic.out 2>&1
'

test_expect_success 'default output shows all three systems' '
	test_debug "cat basic.out" &&
	grep -q "prolog:" basic.out &&
	grep -q "epilog:" basic.out &&
	grep -q "housekeeping:" basic.out
'

test_expect_success 'default output is concise (no scripts shown when not configured)' '
	test_must_fail grep -q "system:" basic.out &&
	test_must_fail grep -q "site:" basic.out
'

test_expect_success 'help output includes verbose option' '
	flux admin system-scripts --help >help.out 2>&1 &&
	grep -q "verbose" help.out &&
	grep -q "color" help.out
'

test_expect_success 'color=never option works' '
	flux admin system-scripts -v --color=never >no_color.out 2>&1 &&
	test_must_fail grep -F "$(printf "\033")" no_color.out
'

test_expect_success 'color=always option produces ANSI codes' '
	flux admin system-scripts -v --color=always >with_color.out 2>&1 &&
	grep -F "$(printf "\033")" with_color.out
'

test_expect_success 'NO_COLOR environment variable works' '
	NO_COLOR=1 flux admin system-scripts -v >no_color_env.out 2>&1 &&
	test_must_fail grep -F "$(printf "\033")" no_color_env.out
'

# Test with script files using overridden paths
test_expect_success 'create test script directories' '
	mkdir -p test_scripts/flux/prolog.d &&
	mkdir -p test_scripts/flux/epilog.d &&
	mkdir -p test_scripts/flux/housekeeping.d &&
	mkdir -p test_scripts/flux/system/prolog.d &&
	mkdir -p test_scripts/flux/system/epilog.d &&
	mkdir -p test_scripts/flux/system/housekeeping.d
'

test_expect_success 'create executable test scripts' '
	cat >test_scripts/flux/prolog.d/01-test.sh <<-"EOF" &&
	#!/bin/bash
	echo "test prolog"
	EOF
	chmod +x test_scripts/flux/prolog.d/01-test.sh &&
	cat >test_scripts/flux/system/prolog.d/50-site.sh <<-"EOF" &&
	#!/bin/bash
	echo "site prolog"
	EOF
	chmod +x test_scripts/flux/system/prolog.d/50-site.sh &&
	cat >test_scripts/flux/epilog.d/02-test.sh <<-"EOF" &&
	#!/bin/bash
	echo "test epilog"
	EOF
	chmod +x test_scripts/flux/epilog.d/02-test.sh
'

test_expect_success 'create non-executable script' '
	cat >test_scripts/flux/system/epilog.d/broken.sh <<-"EOF" &&
	#!/bin/bash
	echo "broken"
	EOF
	chmod -x test_scripts/flux/system/epilog.d/broken.sh
'

test_expect_success 'create legacy housekeeping script' '
	cat >test_scripts/flux/system/housekeeping <<-"EOF" &&
	#!/bin/bash
	echo "legacy housekeeping"
	EOF
	chmod +x test_scripts/flux/system/housekeeping
'

test_expect_success 'scripts in sorted order' '
	cat >test_scripts/flux/prolog.d/00-first.sh <<-"EOF" &&
	#!/bin/bash
	EOF
	chmod +x test_scripts/flux/prolog.d/00-first.sh &&
	cat >test_scripts/flux/prolog.d/99-last.sh <<-"EOF" &&
	#!/bin/bash
	EOF
	chmod +x test_scripts/flux/prolog.d/99-last.sh
'

test_expect_success 'verbose mode shows scripts even when not configured' '
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts >normal.out 2>&1 &&
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts -v >verbose.out 2>&1 &&
	test_debug "cat normal.out" &&
	test_debug "cat verbose.out" &&
	grep "not configured" normal.out &&
	grep "not configured" verbose.out &&
	test_must_fail grep "system:" normal.out &&
	grep "system:.*prolog.d" verbose.out
'

test_expect_success 'command scans test script directories with env override' '
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts -v >scripts.out &&
	test_debug "cat scripts.out"
'

test_expect_success 'system scripts shown in output' '
	grep "system:.*test_scripts/flux/prolog.d" scripts.out
'

test_expect_success 'site scripts shown in output' '
	grep "site:.*test_scripts/flux/system/epilog.d" scripts.out
'

test_expect_success 'scripts appear in sorted order' '
	sed -n "/system:.*prolog.d/,/site:/p" scripts.out >prolog_scripts.out &&
	grep -o "[0-9][0-9]-.*\.sh" prolog_scripts.out >order.out &&
	cat >expected_order.out <<-EOF &&
	00-first.sh
	01-test.sh
	99-last.sh
	EOF
	test_cmp expected_order.out order.out
'

test_expect_success 'non-executable script marked correctly' '
	grep "✗ not executable.*broken.sh" scripts.out
'

test_expect_success 'legacy script shown with path and note' '
	grep "✓.*test_scripts/flux/system/housekeeping.*(legacy, skips site scripts)" scripts.out
'

test_expect_success 'directories are ignored in script scan' '
	mkdir -p test_scripts/flux/prolog.d/subdir &&
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts -v >nosubdir.out 2>&1 &&
	test_must_fail grep -q "subdir" nosubdir.out
'

test_expect_success 'create legacy prolog to test site script skipping' '
	cat >test_scripts/flux/system/prolog <<-"EOF" &&
	#!/bin/bash
	echo "legacy prolog"
	EOF
	chmod +x test_scripts/flux/system/prolog &&
	cat >test_scripts/flux/system/prolog.d/should-be-skipped.sh <<-"EOF" &&
	#!/bin/bash
	echo "should not appear"
	EOF
	chmod +x test_scripts/flux/system/prolog.d/should-be-skipped.sh
'

test_expect_success 'legacy script skips site scripts' '
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts -v >legacy_skip.out 2>&1 &&
	sed -n "/^.* prolog:/,/^.* epilog:/p" \
		legacy_skip.out >prolog_legacy.out &&
	test_debug "cat prolog_legacy.out" &&
	grep "legacy, skips site scripts" prolog_legacy.out &&
	test_must_fail grep "should-be-skipped" prolog_legacy.out
'

test_expect_success 'system scripts still shown with legacy present' '
	grep "system:.*test_scripts/flux/prolog.d" prolog_legacy.out
'

# Test imp_path mismatch detection
test_expect_success 'load config with exec.imp matching test libexecdir' '
	flux config load <<-EOF
	[exec]
	imp = "$(pwd)/test_scripts/flux/flux-imp"
	EOF
'

test_expect_success 'no mismatch warning when imp_path matches libexecdir' '
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts -v >no_mismatch.out 2>&1 &&
	test_debug "cat no_mismatch.out" &&
	test_must_fail grep -i "mismatch" no_mismatch.out
'

test_expect_success 'load config with exec.imp NOT matching test libexecdir' '
	flux config load <<-EOF
	[exec]
	imp = "/different/path/flux/flux-imp"
	EOF
'

test_expect_success 'mismatch warning shown when imp_path differs from libexecdir' '
	FLUX_SYSTEM_SCRIPTS_LIBEXECDIR=$(pwd)/test_scripts/flux \
	FLUX_SYSTEM_SCRIPTS_CONFDIR=$(pwd)/test_scripts/flux \
	flux admin system-scripts -v >mismatch.out 2>&1 &&
	test_debug "cat mismatch.out" &&
	grep -i "mismatch" mismatch.out &&
	grep -i "scripts may not be found" mismatch.out
'

test_expect_success 'mismatch warning only shown once (for prolog, not epilog)' '
	grep -c "mismatch" mismatch.out >count.out &&
	test $(cat count.out) -eq 1
'

test_expect_success 'unload exec.imp config for remaining tests' '
	flux config load <<-EOF
	EOF
'

# Test with live configuration
test_expect_success 'load perilog plugin for config tests' '
	flux jobtap load perilog.so
'

test_expect_success 'load prolog configuration' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = [ "true" ]
	timeout = "30m"
	EOF
	flux admin system-scripts >with_prolog.out 2>&1 &&
	test_debug "cat with_prolog.out"
'

test_expect_success 'output shows prolog as enabled' '
	grep "prolog: enabled" with_prolog.out
'

test_expect_success 'output shows prolog mode' '
	grep "per-rank=false" with_prolog.out
'

test_expect_success 'custom command shown in output' '
	grep "command: true" with_prolog.out
'

test_expect_success 'custom command does not show scripts' '
	test_must_fail grep "system:" with_prolog.out &&
	test_must_fail grep "site:" with_prolog.out
'

test_expect_success 'load prolog with default flux-imp command' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = [ "flux-imp", "run", "prolog" ]
	EOF
	flux admin system-scripts >default_cmd.out 2>&1 &&
	test_debug "cat default_cmd.out"
'

test_expect_success 'default command shows scripts' '
	grep "prolog: enabled" default_cmd.out &&
	test_must_fail grep "command:" default_cmd.out
'

test_expect_success 'load per-rank prolog configuration' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = [ "hostname" ]
	per-rank = true
	timeout = "10m"
	EOF
	flux admin system-scripts >per_rank.out 2>&1 &&
	test_debug "cat per_rank.out"
'

test_expect_success 'output shows per-rank mode' '
	grep "per-rank=true" per_rank.out
'

test_expect_success 'load epilog configuration' '
	flux config load <<-EOF &&
	[job-manager.epilog]
	command = [ "true" ]
	EOF
	flux admin system-scripts >with_epilog.out 2>&1 &&
	test_debug "cat with_epilog.out"
'

test_expect_success 'output shows epilog as enabled' '
	grep "epilog: enabled" with_epilog.out
'

test_expect_success 'epilog shows as enabled with per-rank setting' '
	grep "per-rank=false" with_epilog.out
'

test_expect_success 'load both prolog and epilog' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = [ "true" ]
	[job-manager.epilog]
	command = [ "true" ]
	EOF
	flux admin system-scripts >both.out 2>&1 &&
	test_debug "cat both.out"
'

test_expect_success 'both systems show as enabled' '
	grep "prolog: enabled" both.out &&
	grep "epilog: enabled" both.out
'

test_expect_success 'load housekeeping configuration' '
	flux config load <<-EOF &&
	[job-manager.housekeeping]
	command = [ "true" ]
	release-after = "5m"
	EOF
	flux admin system-scripts >with_hk.out 2>&1 &&
	test_debug "cat with_hk.out"
'

test_expect_success 'housekeeping shows as enabled' '
	grep "housekeeping: enabled" with_hk.out
'

test_expect_success 'housekeeping shows release-after setting' '
	grep "release after 5m" with_hk.out
'

test_expect_success 'housekeeping custom command shown' '
	grep "command: true" with_hk.out
'

test_expect_success 'load housekeeping with default flux-imp command' '
	flux config load <<-EOF &&
	[job-manager.housekeeping]
	command = [ "flux-imp", "run", "housekeeping" ]
	EOF
	flux admin system-scripts -v >hk_default.out 2>&1 &&
	test_debug "cat hk_default.out"
'

test_expect_success 'housekeeping default command shows scripts in verbose mode' '
	grep "housekeeping: enabled" hk_default.out &&
	test_must_fail grep "command:" hk_default.out
'

test_expect_success 'clear config returns to not configured' '
	echo "{}" | flux config load &&
	flux admin system-scripts >cleared.out 2>&1 &&
	test_debug "cat cleared.out" &&
	grep "prolog: not configured" cleared.out &&
	grep "epilog: not configured" cleared.out &&
	grep "housekeeping: not configured" cleared.out
'

test_expect_success 'invalid config does not break command' '
	test_must_fail flux config load <<-EOF 2>load_error.out &&
	[job-manager.prolog]
	command = "not-an-array"
	EOF
	test_debug "cat load_error.out" &&
	flux admin system-scripts >after_error.out 2>&1 &&
	test_debug "cat after_error.out" &&
	test -s after_error.out
'

test_expect_success 'unload perilog plugin' '
	flux jobtap remove perilog.so
'

test_expect_success 'configure prolog without plugin loaded' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = [ "true" ]
	EOF
	flux admin system-scripts >no_plugin.out 2>&1 &&
	test_debug "cat no_plugin.out"
'

test_expect_success 'shows warning when configured but plugin not loaded' '
	grep "configured but inactive" no_plugin.out &&
	grep "perilog plugin not loaded" no_plugin.out
'

test_expect_success 'reload perilog plugin for rank 1 test' '
	flux jobtap load perilog.so
'

test_expect_success 'configure prolog for rank 1 test' '
	flux config load <<-EOF
	[job-manager.prolog]
	command = [ "true" ]
	per-rank = true
	EOF
'

test_expect_success 'command works from rank 1' '
	flux exec -r 1 flux admin system-scripts >rank1.out 2>&1 &&
	test_debug "cat rank1.out"
'

test_expect_success 'rank 1 shows correct configuration' '
	grep "prolog: enabled" rank1.out &&
	grep "per-rank=true" rank1.out &&
	grep "command: true" rank1.out
'

# Test as non-instance owner (guest user)
test_expect_success 'configure prolog and housekeeping for guest test' '
	flux config load <<-EOF
	[job-manager.prolog]
	command = [ "true" ]
	per-rank = true
	[job-manager.housekeeping]
	command = [ "true" ]
	release-after = "5m"
	EOF
'

test_expect_success 'simulate guest user with FLUX_HANDLE_USERID and ROLEMASK' '
	FLUX_HANDLE_USERID=9999 FLUX_HANDLE_ROLEMASK=0x2 \
		flux admin system-scripts >guest.out 2>&1 &&
	test_debug "cat guest.out"
'

test_expect_success 'guest sees warning about perilog plugin status' '
	grep -i "only instance owner can query perilog.so status" guest.out
'

test_expect_success 'guest still sees prolog configuration from broker config' '
	grep "prolog: enabled" guest.out &&
	grep "per-rank=true" guest.out
'

test_expect_success 'guest sees housekeeping configuration' '
	grep "housekeeping: enabled" guest.out &&
	grep "release after 5m" guest.out
'

test_done

