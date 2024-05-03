#!/bin/sh

test_description='flux-resource status output format tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# Use a static format to avoid breaking output if default flux-resource status
#  format ever changes
FORMAT="{state:>10} {nnodes:>6} {ranks:<15} {nodelist}"
INPUTDIR=${SHARNESS_TEST_SRCDIR}/flux-resource/status

export FLUX_PYCLI_LOGLEVEL=10

for input in ${INPUTDIR}/*.json; do
    name=$(basename ${input%%.json})
    test_expect_success "flux-resource status input check: $name" '
        base=${input%%.json} &&
        expected=${base}.expected &&
        name=$(basename $base) &&
        flux resource status -o "$FORMAT" \
            --from-stdin < $input > $name.output 2>&1 &&
        test_debug "cat $name.output" &&
        test_cmp $expected $name.output
    '
done

# Special case for --queue handling using fluke.json and fluke.config
FLUKE_CONFIG=${SHARNESS_TEST_SRCDIR}/flux-resource/status/fluke.config
FLUKE_INPUT=${SHARNESS_TEST_SRCDIR}/flux-resource/status/fluke.json
test_expect_success 'flux-resource status: -q, --queue works' '
	flux resource status --queue=debug \
		--format="$FORMAT" \
		--from-stdin --config-file=$FLUKE_CONFIG \
		< $FLUKE_INPUT > fluke-debug.output &&
	cat <<-EOF >fluke-debug.expected &&
	     STATE NNODES RANKS           NODELIST
	     avail      5 93,96-99        fluke[96,99-102]
	    avail*      3 94-95,100       fluke[97-98,103]
	EOF
	test_cmp fluke-debug.expected fluke-debug.output &&
	flux resource status --queue=batch \
		--format="$FORMAT" \
		--from-stdin --config-file=$FLUKE_CONFIG \
		< $FLUKE_INPUT > fluke-batch.output &&
	cat <<-EOF >fluke-batch.expected &&
	     STATE NNODES RANKS           NODELIST
	     avail     80 3-13,16-20,22-39,41-57,59-61,63-72,75,77-88,90-92 fluke[6-16,19-23,25-42,44-60,62-64,66-75,78,80-91,93-95]
	    avail*      8 14,21,58,62,73-74,76,89 fluke[17,24,61,65,76-77,79,92]
	   drained      2 15,40           fluke[18,43]
	EOF
	test_cmp fluke-batch.expected fluke-batch.output
'
test_expect_success 'flux-resource status -q, --queue fails on invalid queue' '
	test_must_fail flux resource status --queue=foo \
		--format="$FORMAT" \
		--from-stdin --config-file=$FLUKE_CONFIG \
		< $FLUKE_INPUT 2>invalidqueue.err &&
	grep "foo: no such queue" invalidqueue.err
'

#  Ensure all tested inputs can also work with --include
#  We simply restrict to rank 0 and then ensure {ranks} returns only 0
for input in ${INPUTDIR}/*.json; do
    name=$(basename ${input%%.json})
    test_expect_success "flux-resource status input --include check: $name" '
        base=${input%%.json} &&
        name=$(basename $base)-i &&
        flux resource status -o "{ranks} {nodelist}" --include=0 \
            --from-stdin < $input > $name.output 2>&1 &&
        test_debug "cat $name.output" &&
	grep "^0[^,-]" $name.output
    '
done


test_expect_success 'flux-resource status: header included with all formats' '
	cat <<-EOF >headers.expected &&
	state==STATE
	status=STATUS
	nnodes==NNODES
	ranks==RANKS
	nodelist==NODELIST
	reason==REASON
	timestamp==TIME
	EOF
	sed "s/\(.*\)==.*/\1=={\1}/" headers.expected > headers.fmt &&
        flux resource status --from-stdin --format="$(cat headers.fmt)" \
            > headers.output < /dev/null &&
        test_cmp headers.expected headers.output
'

test_expect_success 'flux resource status: FLUX_RESOURCE_STATUS_FORMAT_DEFAULT works' '
	FLUX_RESOURCE_STATUS_FORMAT_DEFAULT="{nodelist} {nodelist}" \
		flux resource status --from-stdin > default_override.out < /dev/null &&
	grep "NODELIST NODELIST" default_override.out
'

test_expect_success 'flux resource status: FLUX_RESOURCE_STATUS_FORMAT_DEFAULT works w/ named format' '
	FLUX_RESOURCE_STATUS_FORMAT_DEFAULT=long \
		flux resource status --from-stdin > default_override_named.out < /dev/null &&
	grep "REASON" default_override_named.out
'

test_expect_success 'flux-resource status: --no-header works' '
	INPUT=${INPUTDIR}/example.json &&
	name=no-header &&
	flux resource status -s exclude --no-header --from-stdin < $INPUT \
	    > ${name}.out &&
	test_debug "cat ${name}.out" &&
	test $(wc -l < ${name}.out) -eq 1
'

test_expect_success 'flux-resource status: -o {reason} works' '
	INPUT=${INPUTDIR}/drain.json &&
	name=reason &&
	flux resource status --from-stdin -s drain -no {reason} \
	    < $INPUT > ${name}.out &&
	test_debug "cat ${name}.out" &&
	test $(wc -l < ${name}.out) -eq 2
'

test_expect_success 'flux-resource status -o long shows REASON field' '
	INPUT=${INPUTDIR}/drain.json &&
	name=long &&
	flux resource status --from-stdin < $INPUT > ${name}.out &&
	flux resource status -o long --from-stdin < $INPUT > ${name}-long.out &&
	test_debug "echo default:; cat ${name}.out" &&
	test_debug "echo long:; cat ${name}-long.out" &&
	test_must_fail grep REASON ${name}.out &&
	grep REASON ${name}-long.out &&
	test $(grep -c drained  ${name}.out) -eq 1 &&
	test $(grep -c drained ${name}-long.out) -eq 2
'
test_expect_success 'flux-resource status: -s always displays that state' '
	INPUT=${INPUTDIR}/simple.json &&
	flux resource status --from-stdin -s exclude -o {state} --no-header \
	    < $INPUT | grep exclude
'
test_expect_success 'flux-resource status: --skip-empty works' '
	INPUT=${INPUTDIR}/simple.json &&
	flux resource status --from-stdin --skip-empty \
	    -s exclude -o {state} --no-header \
	    < $INPUT >skip-empty.out &&
	test_debug "cat skip-empty.out" &&
	test_must_be_empty skip-empty.out
'
test_expect_success 'flux-resource status: -s help displays states list' '
	flux resource status --from-stdin -s help < /dev/null 2>&1 \
	    | grep -i valid
'
test_expect_success 'flux-resource status: invalid state generates error' '
	test_expect_code 1 flux resource status --from-stdin -s frelled \
	    < /dev/null >bad-state.out 2>&1 &&
	test_debug "cat bad-state.out" &&
	grep "Invalid resource state frelled specified" bad-state.out
'

test_expect_success 'flux-resource status: {status} field works' '
	INPUT=${INPUTDIR}/example.json &&
	flux resource status --from-stdin -o "{status:>8} {nnodes}" < $INPUT \
		>status.output &&
	cat <<-EOF >status.expected &&
	  STATUS NNODES
	  online 4
	 offline 1
	EOF
	test_cmp status.expected status.output
'

test_expect_success 'flux-resource status: {up} field works' '
	INPUT=${INPUTDIR}/example.json &&
	flux resource status --from-stdin -o "{up:>2} {nnodes}" < $INPUT \
		>up.output &&
	cat <<-EOF >up.expected &&
	UP NNODES
	 ✔ 4
	 ✗ 1
	EOF
	test_cmp up.expected up.output
'

test_expect_success 'flux-resource status: {up.ascii} field works' '
	INPUT=${INPUTDIR}/example.json &&
	flux resource status --from-stdin -o "{up.ascii:>2} {nnodes}" < $INPUT \
		>up.ascii.output &&
	cat <<-EOF >up.ascii.expected &&
	UP NNODES
	 y 4
	 n 1
	EOF
	test_cmp up.ascii.expected up.ascii.output
'

test_expect_success 'flux-resource status: {color_up} works' '
	INPUT=${INPUTDIR}/example.json &&
	flux resource status -n --color --states=offline --from-stdin \
		-o "{color_up}{nnodes}{color_off}" \
		<$INPUT >color.out &&
	test_debug "cat color.out" &&
	grep "^.*1" color.out &&
	flux resource status -n --color --states=avail --from-stdin \
		-o "{color_up}{nnodes}{color_off}" \
		<$INPUT >color.out &&
	test_debug "cat color.out" &&
	grep "^.*2" color.out
'

test_expect_success 'flux-resource status: similar lines are combined' '
	INPUT=${INPUTDIR}/drain.json &&
	flux resource status -n --color --states=drain --from-stdin \
		--skip-empty \
		-o "{state:>8} {nnodes}" \
		<$INPUT >combined.out &&
	test_debug "cat combined.out" &&
	test $(wc -l < combined.out) -eq 1 &&
	flux resource status -n --color --states=drain --from-stdin \
		--skip-empty \
		-o "{state:>8} {nnodes:>6} {reason}" \
		<$INPUT >combined2.out &&
	test_debug "cat combined2.out" &&
	test $(wc -l < combined2.out) -eq 2
'

test_expect_success 'flux-resource status: lines are combined based on format' '
	INPUT=${INPUTDIR}/drain.json &&
	flux resource status -n --color --states=drain --from-stdin \
		--skip-empty \
		-o "{timestamp} {state:>8} {nnodes}" \
		<$INPUT >ts-detailed.out &&
	test_debug "cat ts-detailed.out" &&
	test $(wc -l < ts-detailed.out) -eq 2 &&
	flux resource status -n --color --states=drain --from-stdin \
		--skip-empty \
		-o "{timestamp!d:%F::<12} {state:>8} {nnodes}" \
		<$INPUT >ts-detailed2.out &&
	test_debug "cat ts-detailed2.out" &&
	cat <<-EOF >ts-detailed2.expected &&
	2020-12-09    drained 2
	EOF
	test_cmp ts-detailed2.expected ts-detailed2.out
'
test_expect_success 'flux-resource status: --include works with ranks' '
	INPUT=${INPUTDIR}/drain.json &&
	flux resource status --include=1,3 --from-stdin \
		-no "{nnodes}" <$INPUT >drain-include.out &&
	test_debug "cat drain-include.out" &&
	test "$(cat drain-include.out)" = "2"
'
test_expect_success 'flux-resource status: --include works with hostnames' '
	INPUT=${INPUTDIR}/drain.json &&
	flux resource status --include=foo[1,3] --from-stdin \
		-no "{nodelist}" <$INPUT >drain-include-host.out &&
	test_debug "cat drain-include-host.out" &&
	test "$(cat drain-include-host.out)" = "foo[1,3]"
'
test_expect_success 'flux-resource status: --include works with invalid host' '
	INPUT=${INPUTDIR}/drain.json &&
	flux resource status --include=foo7 --from-stdin \
		-no "{nodelist}" <$INPUT >drain-empty.out 2>&1 &&
	test_must_be_empty drain-fail.out
'
test_done
