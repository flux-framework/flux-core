#!/bin/sh

test_description='flux-resource list tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 full

# Use a static format to avoid breaking output if default flux-resource list
#  format ever changes
FORMAT="{state:>10} {properties:<10} {nnodes:>6} {ncores:>8} {ngpus:>8}"

for input in ${SHARNESS_TEST_SRCDIR}/flux-resource/list/*.json; do
    testname=$(basename ${input%%.json}) &&
    base=${input%%.json} &&
    name=$(basename $base) &&
    test_expect_success "flux-resource list input check: $testname" '
        flux resource list -o "$FORMAT" \
            --from-stdin < $input > $name.output 2>&1 &&
        test_debug "cat $name.output" &&
        test_cmp ${base}.expected $name.output
    '
    test_expect_success "flux-resource info input check: $testname" '
        flux resource info --from-stdin < $input > ${name}-info.output 2>&1 &&
        test_debug "cat ${name}-info.output" &&
        test_cmp ${base}-info.expected ${name}-info.output
    '
done

test_expect_success 'create test input with properties' '
	cat <<-EOF >properties-test.in
	{
	  "all": {
	    "version": 1,
	    "execution": {
	      "R_lite": [
	        {
	          "rank": "0-3",
	          "children": {
	            "core": "0-3"
	          }
	        }
	      ],
	      "starttime": 0,
	      "expiration": 0,
	      "nodelist": [
	        "asp,asp,asp,asp"
	      ],
	      "properties": {
	        "foo": "2-3",
	        "xx": "1-2"
	      }
	    }
	  },
	  "allocated": {
	    "version": 1,
	    "execution": {
	      "R_lite": [
	        {
	          "rank": "1",
	          "children": {
	            "core": "0"
	          }
	        }
	      ],
	      "starttime": 0,
	      "expiration": 0,
	      "nodelist": [
	        "asp"
	      ],
	      "properties": {
	        "xx": "1"
	      }
	    }
	  },
	  "down": {
	    "version": 1,
	    "execution": {
	      "R_lite": [
	        {
	          "rank": "1",
	          "children": {
	            "core": "0-3"
	          }
	        }
	      ],
	      "starttime": 0,
	      "expiration": 0,
	      "nodelist": [
	        "asp"
	      ],
	      "properties": {
	        "xx": "1"
	      }
	    }
	  }
	}
	EOF
'

test_expect_success 'flux resource list -no {properties} works' '
	flux resource list -no {properties} \
		--from-stdin <properties-test.in >properties.out &&
	test $(cat properties.out) = "xx,foo"
'
test_expect_success 'flux resource list -no {properties:>4.4+} works' '
	flux resource list -no "{properties:>5.5+}" \
		--from-stdin <properties-test.in >properties-trunc.out &&
	test_debug "cat properties-trunc.out" &&
	test $(cat properties-trunc.out) = "xx,f+" &&
	flux resource list -no "{properties:>5.5h+}" \
		--from-stdin <properties-test.in >properties-trunc+h.out &&
	test_debug "cat properties-trunc+h.out" &&
	test $(cat properties-trunc.out) = "xx,f+"
'
test_expect_success 'flux resource list -o rlist works' '
	flux resource list -o rlist \
		--from-stdin <properties-test.in >rlist.out &&
	test_debug "cat rlist.out" &&
	grep -i list rlist.out
'
test_expect_success 'configure queues and resource split amongst queues' '
	flux R encode -r 0-3 -p batch:0-1 -p debug:2-3 \
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.debug]
	requires = [ "debug" ]
	EOT
	flux queue start --all &&
	flux module unload sched-simple &&
	flux module reload resource &&
	flux module load sched-simple
'
test_expect_success 'flux resource list default lists queues' '
	flux resource list | grep QUEUE
'
test_expect_success 'flux resource lists expected queues (single)' '
	flux resource list -o "{state} {nnodes} {queue}" > listqueue_single.out &&
	test $(grep -c "free 2 batch" listqueue_single.out) -eq 1 &&
	test $(grep -c "free 2 debug" listqueue_single.out) -eq 1
'
test_expect_success 'flux resource lists expected properties (single)' '
	flux resource list -o "{state} {nnodes} {properties}" > listprop_single.out &&
	test $(grep -c "free 2 batch" listprop_single.out) -eq 1 &&
	test $(grep -c "free 2 debug" listprop_single.out) -eq 1
'
# no properties should be output, leading to a single line for all 4 "free" nodes
test_expect_success 'flux resource lists no properties in propertiesx (single)' '
	flux resource list -o "{state} {nnodes} {propertiesx}" > listpropx_single.out &&
	grep "free 4" listpropx_single.out
'
test_expect_success 'run a few jobs' '
	flux mini submit -q batch sleep 30 > job1A.id &&
	flux mini submit -q debug sleep 30 > job1B.id
'
test_expect_success 'flux resource lists expected queues in states (single)' '
	flux resource list -o "{state} {nnodes} {queue}" > list2.out &&
	test $(grep -c "free 1 batch" list2.out) -eq 1 &&
	test $(grep -c "free 1 debug" list2.out) -eq 1 &&
	test $(grep -c "allocated 1 batch" list2.out) -eq 1 &&
	test $(grep -c "allocated 1 debug" list2.out) -eq 1
'
test_expect_success 'cleanup jobs' '
	flux job cancel $(cat job1A.id) $(cat job1B.id)
'
test_expect_success 'configure queues and resource split amongst queues' '
	flux R encode -r 0-3 -p all:0-3 -p batch:0-1 -p debug:2-3 \
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.all]
	requires = [ "all" ]
	[queues.batch]
	requires = [ "batch" ]
	[queues.debug]
	requires = [ "debug" ]
	EOT
	flux queue start --all &&
	flux module unload sched-simple &&
	flux module reload resource &&
	flux module load sched-simple
'
# we can't predict listing order of queues/properties, so we grep counts
test_expect_success 'flux resource lists expected queues (multi)' '
	flux resource list -o "{state} {nnodes} {queue}" > listqueue_multi.out &&
	test $(grep "free 2" listqueue_multi.out | grep -c batch) -eq 1 &&
	test $(grep "free 2" listqueue_multi.out | grep -c debug) -eq 1 &&
	test $(grep "free 2" listqueue_multi.out | grep -c all) -eq 2
'
test_expect_success 'flux resource lists expected properties (multi)' '
	flux resource list -o "{state} {nnodes} {properties}" > listprop_multi.out &&
	test $(grep "free 2" listprop_multi.out | grep -c batch) -eq 1 &&
	test $(grep "free 2" listprop_multi.out | grep -c debug) -eq 1 &&
	test $(grep "free 2" listprop_multi.out | grep -c all) -eq 2
'
# no properties should be output, leading to a single line for all 4 "free" nodes
test_expect_success 'flux resource lists no properties in propertiesx (multi)' '
	flux resource list -o "{state} {nnodes} {propertiesx}" > listpropx_multi.out &&
	grep "free 4" listpropx_multi.out
'
test_expect_success 'configure queues and resource with extra property' '
	flux R encode -r 0-3 -p batch:0-1 -p debug:2-3 -p foo:0-3\
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.debug]
	requires = [ "debug" ]
	EOT
	flux queue start --all &&
	flux module unload sched-simple &&
	flux module reload resource &&
	flux module load sched-simple
'
test_expect_success 'flux resource lists expected queues (extraprop)' '
	flux resource list -o "{state} {nnodes} {queue}" > listqueue_extraprop.out &&
	grep "free 2 batch" listqueue_extraprop.out &&
	grep "free 2 debug" listqueue_extraprop.out
'
# we can't predict listing order of properties, so we grep counts
test_expect_success 'flux resource lists expected properties (extraprop)' '
	flux resource list -o "{state} {nnodes} {properties}" > listprop_extraprop.out &&
	test $(grep "free 2" listprop_extraprop.out | grep -c batch) -eq 1 &&
	test $(grep "free 2" listprop_extraprop.out | grep -c debug) -eq 1 &&
	test $(grep "free 2" listprop_extraprop.out | grep -c foo) -eq 2
'
# only "foo" property should be output with properties, leading to
# uniq line with 4 free nodes
test_expect_success 'flux resource lists no properties in propertiesx (extraprop)' '
	flux resource list -o "{state} {nnodes} {propertiesx}" > listpropx_extraprop.out &&
	grep "free 4 foo" listpropx_extraprop.out
'
test_done
