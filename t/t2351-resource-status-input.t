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

test_expect_success 'flux-resource status: -v displays empty states' '
	INPUT=${INPUTDIR}/simple.json &&
	name=verbose &&
	flux resource status -o {nnodes} --from-stdin \
	    < $INPUT > ${name}-no-v.out &&
	flux resource status -v -o {nnodes} --from-stdin \
	    < $INPUT > ${name}-v.out &&
	test_must_fail grep ^0 ${name}-no-v.out &&
	grep ^0 ${name}-v.out
'

test_expect_success 'flux-resource status: -vv displays REASON field' '
	INPUT=${INPUTDIR}/drain.json &&
	name=vv &&
	flux resource status --from-stdin < $INPUT > ${name}.out &&
	flux resource status -vv --from-stdin < $INPUT > ${name}-vv.out &&
	test_debug "echo no verbose:; cat ${name}.out" &&
	test_debug "echo verbose:; cat ${name}-vv.out" &&
	test_must_fail grep REASON ${name}.out &&
	grep REASON ${name}-vv.out &&
	test $(grep -c drained  ${name}.out) -eq 1 &&
	test $(grep -c drained ${name}-vv.out) -eq 2
'

test_expect_success 'flux-resource status: -o {reason} works w/out -vv' '
	INPUT=${INPUTDIR}/drain.json &&
	name=reason &&
	flux resource status --from-stdin -s drain -no {reason} \
	    < $INPUT > ${name}.out &&
	test_debug "cat ${name}.out" &&
	test $(wc -l < ${name}.out) -eq 2
'

test_expect_success 'flux-resource status: -s always displays that state' '
	INPUT=${INPUTDIR}/simple.json &&
	flux resource status --from-stdin -s exclude -o {state} --no-header \
	    < $INPUT | grep exclude
'

test_expect_success 'flux-resource status: -s help displays states list' '
	flux resource status --from-stdin -s help < /dev/null 2>&1 \
	    | grep -i valid
'
test_expect_success 'flux-resource status: -o help displays format list' '
	flux resource status --from-stdin -o help < /dev/null 2>&1 \
	    | grep -i valid
'

test_expect_success 'flux-resource status: invalid state generates error' '
	test_expect_code 1 flux resource status --from-stdin -s frelled \
	    < /dev/null >bad-state.out 2>&1 &&
	test_debug "cat bad-state.out" &&
	grep "Invalid resource state frelled specified" bad-state.out
'

test_done
