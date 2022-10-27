#!/bin/sh

test_description='flux-resource status output format tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# Use a static format to avoid breaking output if default flux-resource status
#  format ever changes
FORMAT="{state:>10} {nnodes:>6} {ranks:<15} {nodelist}"
INPUTDIR=${SHARNESS_TEST_SRCDIR}/flux-resource/status

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

test_expect_success 'flux-resource status: header included with all formats' '
	cat <<-EOF >headers.expected &&
	state==STATUS
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

test_expect_success 'flux-resource status: --no-header works' '
	INPUT=${INPUTDIR}/example.json &&
	name=no-header &&
	flux resource status -s all --no-header --from-stdin < $INPUT \
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

test_done
