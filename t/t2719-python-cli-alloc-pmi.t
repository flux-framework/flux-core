#!/bin/sh

test_description='flux alloc/batch pmi option precedence tests'

. $(dirname $0)/sharness.sh

# Start an instance with 8 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=2
test_under_flux 4 job

test_expect_success 'flux alloc: dry-run shows pmi=simple in jobspec by default' '
	flux alloc -n1 --dry-run true | \
	    jq -e ".attributes.system.shell.options.pmi == \"simple\""
'

test_expect_success 'flux batch: dry-run shows pmi=simple in jobspec by default' '
	flux batch -n1 --dry-run --wrap hostname | \
	    jq -e ".attributes.system.shell.options.pmi == \"simple\""
'

test_expect_success 'flux alloc: dry-run shows overridden pmi' '
	flux alloc -n1 -o pmi=off --dry-run true | \
	    jq -e ".attributes.system.shell.options.pmi == \"off\""
'

test_expect_success 'flux alloc: with default pmi=simple, multi-broker instance bootstraps' '
	echo 2 >default.exp &&
	flux alloc -N2 -o cpu-affinity=off flux getattr size >default.out &&
	test_cmp default.exp default.out
'

test_expect_success 'flux batch: with default pmi=simple, multi-broker instance bootstraps' '
	echo 2 >batch-default.exp &&
	jobid=$(flux batch -N2 -o cpu-affinity=off --output=batch-default.out --wrap flux getattr size) &&
	flux job wait-event -t 30 $jobid finish &&
	test_cmp batch-default.exp batch-default.out
'
test_expect_success 'flux alloc: with pmi=off, brokers bootstrap as singletons' '
	cat <<-EOT >pmi-off.exp &&
	1
	1
	EOT
	flux alloc -N2 -o cpu-affinity=off -o pmi=off \
		flux getattr size >pmi-off.out &&
	test_cmp pmi-off.exp pmi-off.out
'

test_expect_success 'flux alloc: user override with -o pmi=simple works' '
	echo 2 >override-simple.exp &&
	flux alloc -N2 -o cpu-affinity=off -o pmi=simple flux getattr size >override-simple.out &&
	test_cmp override-simple.exp override-simple.out
'

test_expect_success 'flux alloc: multiple -o pmi options, last one wins' '
	echo 2 >multiple.exp &&
	flux alloc -N2 -o cpu-affinity=off -o pmi=off -o pmi=simple \
		flux getattr size >multiple.out &&
	test_cmp multiple.exp multiple.out
'

test_expect_success 'flux alloc: last -o pmi=off causes singleton bootstrap' '
	cat >last-off.exp <<-EOT &&
	1
	1
	EOT
	flux alloc -N2 -o cpu-affinity=off -o pmi=simple -o pmi=off \
		flux getattr size >last-off.out &&
	test_cmp last-off.exp last-off.out
'

# Test interaction with initrc that tries to set pmi
test_expect_success 'create initrc that sets pmi=off unconditionally' '
	cat >set-pmi-off-initrc.lua <<-EOF
	-- Try to set pmi to off (would break bootstrap)
	shell.options.pmi = "off"
	EOF
'

test_expect_success 'flux alloc: initrc CAN override jobspec default (not recommended)' '
	cat >initrc-vs-default.exp <<-EOT &&
	1
	1
	EOT
	flux alloc -N2 -o cpu-affinity=off \
		-o initrc=$(pwd)/set-pmi-off-initrc.lua \
		flux getattr size >initrc-vs-default.out &&
	test_cmp initrc-vs-default.exp initrc-vs-default.out
'

test_expect_success 'flux alloc: initrc CAN override command line (not recommended)' '
	cat >cmdline-wins.exp <<-EOT &&
	1
	1
	EOT
	flux alloc -N2 -o cpu-affinity=off \
		-o pmi=simple \
		-o initrc=$(pwd)/set-pmi-off-initrc.lua \
		flux getattr size >cmdline-wins.out &&
	test_cmp cmdline-wins.exp cmdline-wins.out
'

test_expect_success 'create initrc that conditionally sets pmi if not already set' '
	cat >conditional-pmi-initrc.lua <<-EOF
	-- Only set pmi if not already set (recommended pattern for site initrc)
	if not shell.options.pmi then
	    shell.options.pmi = "off"
	end
	EOF
'

test_expect_success 'flux alloc: initrc conditional does not override jobspec default' '
	echo 2 >conditional.exp &&
	flux alloc -N2 -o cpu-affinity=off \
		-o initrc=$(pwd)/conditional-pmi-initrc.lua \
		flux getattr size >conditional.out &&
	test_cmp conditional.exp conditional.out
'

test_done
