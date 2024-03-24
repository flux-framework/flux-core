#!/bin/sh

test_description='flux-resource list tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 full

test_expect_success 'flux resource list: default lists some expected fields' '
	flux resource list > default.out &&
	grep STATE default.out &&
	grep NNODES default.out &&
	grep NCORES default.out
'
test_expect_success 'make the same query using sched.resource-status' '
	FLUX_RESOURCE_LIST_RPC=sched.resource-status \
	FLUX_HANDLE_TRACE=1 \
	flux resource list >sched.out 2>sched.err
'
test_expect_success 'FLUX_RESOURCE_LIST_RPC works' '
	grep sched.resource-status sched.err
'
test_expect_success 'results are the same as before' '
	test_cmp default.out sched.out
'

test_expect_success 'flux resource list: FLUX_RESOURCE_LIST_FORMAT_DEFAULT works' '
	FLUX_RESOURCE_LIST_FORMAT_DEFAULT="{nodelist} {nodelist}" \
		flux resource list > default_override.out &&
	grep "NODELIST NODELIST" default_override.out
'

test_expect_success 'flux resource list: FLUX_RESOURCE_LIST_FORMAT_DEFAULT works w/ named format' '
	FLUX_RESOURCE_LIST_FORMAT_DEFAULT=rlist \
		flux resource list > default_override_named.out &&
	grep -w "LIST" default_override_named.out
'

test_expect_success 'flux resource list: --no-header works' '
	flux resource list --no-header > default_no_header.out &&
	test_must_fail grep STATE default_no_header.out &&
	test_must_fail grep NNODES default_no_header.out &&
	test_must_fail grep NCORES default_no_header.out
'

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
    test_expect_success "flux-resource R input check: $testname" '
        flux resource R --from-stdin < $input > ${name}-R.output 2>&1 &&
        test_debug "cat ${name}-info.output" &&
        test_cmp ${base}.R ${name}-R.output
    '
done

#  Ensure all tested inputs can also work with --include
#  Simply restrict to rank 0 then ensure {ranks} returns only 0
for input in ${SHARNESS_TEST_SRCDIR}/flux-resource/list/*.json; do
    testname=$(basename ${input%%.json}) &&
    base=${input%%.json} &&
    name=$(basename $base) &&
    test_expect_success "flux-resource list input --include check: $name" '
        flux resource list -o "{ranks} {nodelist}" --include=0 \
            --from-stdin < $input >$name-include.output 2>&1 &&
        test_debug "cat $name-include.output" &&
        grep "^0[^,-]" $name-include.output
    '
    test_expect_success "flux-resource info input --include check: $testname" '
        flux resource info --from-stdin -i0 < $input \
            > ${name}-info-include.output 2>&1 &&
        test_debug "cat ${name}-info-include.output" &&
	grep "1 Node" ${name}-info-include.output
    '
    test_expect_success "flux-resource R input ---include check: $testname" '
        flux resource R --from-stdin -i0 < $input \
           > ${name}-info-R.output &&
        test "$(flux R decode --count=node <${name}-info-R.output)" -eq 1
    '
done

test_expect_success 'flux-resource list supports --include' '
	flux resource list -s all -ni 0 >list-include.output &&
	test_debug "cat list-include.output" &&
	test $(wc -l <list-include.output) -eq 1
'
INPUT=${SHARNESS_TEST_SRCDIR}/flux-resource/list/normal-new.json
test_expect_success 'flux-resource list: --include works with ranks' '
	flux resource list -s all -o "{nnodes} {ranks}" -ni 0,3 --from-stdin \
		< $INPUT >include-ranks.out &&
	test_debug "cat include-ranks.out" &&
	grep "^2 0,3" include-ranks.out
'
test_expect_success 'flux-resource list: --include works with hostnames' '
	flux resource list -s all -o "{nnodes} {nodelist}" -ni pi[0,3] \
		--from-stdin < $INPUT >include-hosts.out &&
	test_debug "cat include-hosts.out" &&
	grep "^2 pi\[3,0\]" include-hosts.out
'
test_expect_success 'flux-resource list: -i works with excluded hosts #5266' '
	cat <<-'EOF' >corona.json &&
	{
	  "all": {
	    "execution": {
	      "R_lite": [
	        {
	          "children": {
	            "core": "0-47",
	            "gpu": "0-7"
	          },
	          "rank": "4-124"
	        }
	      ],
	      "expiration": 0,
	      "nodelist": [
	        "corona[171-207,213-296]"
	      ],
	      "properties": {
	        "pbatch": "20-124",
	        "pdebug": "4-19"
	      },
	      "starttime": 0
	    },
	    "version": 1
	  },
	  "allocated": {
	    "execution": {
	      "R_lite": [
	        {
	          "children": {
	            "core": "0-47",
	            "gpu": "0-7"
	          },
	          "rank": "20-27,29-34,36-75,78-84,86-87,90-97,99-111,113-124"
	        }
	      ],
	      "expiration": 0,
	      "nodelist": [
	        "corona[187-194,196-201,203-207,213-247,250-256,258-259,262-269,271-283,285-296]"
	      ],
	      "properties": {
	        "pbatch": "20-27,29-34,36-75,78-84,86-87,90-97,99-111,113-124"
	      },
	      "starttime": 0
	    },
	    "version": 1
	  },
	  "down": {
	    "execution": {
	      "R_lite": [
	        {
	          "children": {
	            "core": "0-47",
	            "gpu": "0-7"
	          },
	          "rank": "5,9,28,35,76-77,85,88-89,98,112"
	        }
	      ],
	      "expiration": 0,
	      "nodelist": [
	        "corona[172,176,195,202,248-249,257,260-261,270,284]"
	      ],
	      "properties": {
	        "pbatch": "28,35,76-77,85,88-89,98,112",
	        "pdebug": "5,9"
	      },
	      "starttime": 0
	    },
	    "version": 1
	  }
	}
	EOF
	NODELIST="corona[176,249,260-261,270,284]" &&
	flux resource list -s all -o "{nodelist}" -ni $NODELIST \
		--from-stdin < corona.json >corona.output &&
	test_debug "cat corona.output" &&
	test "$(cat corona.output)" = "$NODELIST"
'
test_expect_success 'flux-resource list: --include works with invalid host' '
	flux resource list -s all -o "{nnodes} {nodelist}" -ni pi7 \
		--from-stdin < $INPUT >include-invalid-hosts.out &&
	test_debug "cat include-invalid-hosts.out" &&
	grep "^0" include-invalid-hosts.out
'
test_expect_success 'flux-resource R supports --states' '
	flux resource R --from-stdin -s all <$INPUT >all.R &&
	test $(flux R decode --count=node <all.R) -eq 5 &&
	flux resource R --from-stdin -s up <$INPUT >up.R &&
	test $(flux R decode --count=node <up.R) -eq 5 &&
	flux resource R --from-stdin -s down <$INPUT >down.R &&
	test $(flux R decode --count=node <down.R) -eq 0
'
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
	flux submit -q batch sleep 30 > job1A.id &&
	flux submit -q debug sleep 30 > job1B.id
'
test_expect_success 'flux resource lists expected queues in states (single)' '
	flux resource list -o "{state} {nnodes} {queue}" > list2.out &&
	test $(grep -c "free 1 batch" list2.out) -eq 1 &&
	test $(grep -c "free 1 debug" list2.out) -eq 1 &&
	test $(grep -c "allocated 1 batch" list2.out) -eq 1 &&
	test $(grep -c "allocated 1 debug" list2.out) -eq 1
'
test_expect_success 'sched.resource-status produces the same results' '
	FLUX_RESOURCE_LIST_RPC=sched.resource-status \
	flux resource list -o "{state} {nnodes} {queue}" > list2_sched.out &&
	test_cmp list2.out list2_sched.out
'
test_expect_success 'cleanup jobs' '
	flux cancel $(cat job1A.id) $(cat job1B.id)
'
test_expect_success 'configure queues and resource split amongst queues w/ all' '
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
# N.B. we use the queue "every" b/c the word "all" happens to be in
# the word "allocated", annoyingly messing up greps
test_expect_success 'configure queues and resource split amongst queues, add every queue' '
	flux R encode -r 0-3 -p batch:0-1 -p debug:2-3 \
	   | tr -d "\n" \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.every]
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
test_expect_success 'flux resource lists expected queues (every)' '
	flux resource list -o "{state} {nnodes} {queue}" > listqueue_every.out &&
	test $(grep "free 2" listqueue_every.out | grep -c batch) -eq 1 &&
	test $(grep "free 2" listqueue_every.out | grep -c debug) -eq 1 &&
	test $(grep "free 2" listqueue_every.out | grep -c every) -eq 2 &&
	test $(grep "allocated 0" listqueue_every.out | grep -c every) -eq 0 &&
	test $(grep "down 0" listqueue_every.out | grep -c every) -eq 0
'
test_expect_success 'run a few jobs (every)' '
	flux submit -q batch sleep 30 > job2A.id &&
	flux submit -q debug sleep 30 > job2B.id
'
test_expect_success 'flux resource lists expected queues in states (every)' '
	flux resource list -o "{state} {nnodes} {queue}" > listqueue_every2.out &&
	test $(grep "free 1" listqueue_every2.out | grep -c batch) -eq 1 &&
	test $(grep "free 1" listqueue_every2.out | grep -c debug) -eq 1 &&
	test $(grep "free 1" listqueue_every2.out | grep -c every) -eq 2 &&
	test $(grep "allocated 1" listqueue_every2.out | grep -c batch) -eq 1 &&
	test $(grep "allocated 1" listqueue_every2.out | grep -c debug) -eq 1 &&
	test $(grep "allocated 1" listqueue_every2.out | grep -c every) -eq 2 &&
	test $(grep "down 0" listqueue_every2.out | grep -c every) -eq 0
'
test_expect_success 'cleanup jobs' '
	flux cancel $(cat job2A.id) $(cat job2B.id)
'
test_done
