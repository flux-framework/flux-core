#!/bin/sh

test_description='Test command line plugin for RFC 46 job shape'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

test_expect_success 'flux-alloc: a job that does not provide a job shape can run' '
	flux alloc -N 1 flux resource list -no {nnodes} | grep 1
'
test_expect_success 'flux-run: job that calls shape plugin has resources set properly' '
	cat <<-EOF | jq -S . > exp1.json &&
	[
	  {
	    "type": "slot",
	    "count": 4,
	    "label": "task",
	    "with": [
	      {
	        "type": "node",
	        "count": 1
	      }
	    ]
	  }
	]
	EOF
	flux run --resources-shape=slot=4/node --dry-run hostname | jq -S .resources > output1.json &&
	test_cmp exp1.json output1.json
'
test_expect_success 'flux-alloc: job with invalid shape is rejected' '
	test_must_fail flux alloc --resources-shape=slot/ echo hello 2> err.out &&
	grep "flux-alloc: ERROR" err.out
'
test_expect_success 'flux-batch: subinstance job with shape plugin has proper resources' '
	cat <<-EOF | jq -S . > exp2.json &&
	[
	  {
	    "type": "node",
	    "count": 1,
	    "with": [
	      {
	        "type": "slot",
	        "count": 10,
	        "label": "read-db",
	        "with": [
	          {
	            "type": "core",
	            "count": 1
	          },
	          {
	            "type": "memory",
	            "count": 4,
	            "unit": "GB"
	          }
	        ]
	      },
	      {
	        "type": "slot",
	        "count": 1,
	        "label": "db",
	        "with": [
	          {
	            "type": "core",
	            "count": 6
	          },
	          {
	            "type": "memory",
	            "count": 24,
	            "unit": "GB"
	          }
	        ]
	      }
	    ]
	  }
	]
	EOF
	flux batch --resources-shape=node/[slot=10{read-db}/[core\;memory=4{unit:GB}]\;slot{db}/[core=6\;memory=24{unit:GB}]] \
	    --dry-run --wrap hostname | jq -S .resources > output2.json && 
	test_cmp exp2.json output2.json
'
test_expect_success 'flux-submit: providing --resources-json gets the expected resource set' '
	cat <<-EOF | jq -S .resources > r_set.json &&
	{
	  "resources": [
	    {
	      "type": "slot",
	      "count": 10,
	      "label": "task",
	      "with": [
	        {
	          "type": "core",
	          "count": 2
	        }
	      ]
	    }
	  ]
	}	
	EOF
	flux submit --resources-json=r_set.json --dry-run hostname | jq -S .resources > output3.json && 
	test_cmp r_set.json output3.json
'
test_expect_success 'flux-submit: providing invalid json gets rejected' '
	cat <<-EOF  > r_set2.json &&
	{
	  "resources": [
	    {
	      "type": "slot",
	      "count": 10,
	      "label": "task",
	      "with": [
	        {
	          "type": "core"aloskdjfioqwejio;fwqeio;,
	          "count": 2
	        }
	      ]
	    }
	  ],
	}	
	EOF
	test_must_fail flux submit --resources-json=r_set2.json hostname 2> output4.err &&
	grep "flux-submit: ERROR: Expecting " output4.err
'
test_expect_success 'flux-alloc: job request with both shape and json is rejected' '
	cat <<-EOF | jq -S . > exp4.json &&
	[
	  {
	    "type": "slot",
	    "count": 4,
	    "label": "task",
	    "with": [
	      {
	        "type": "node",
	        "count": 1
	      }
	    ]
	  }
	]
	EOF
	test_must_fail flux run --resources-shape=slot=4/node --resources-json=exp4.json --dry-run hostname 2> err4.err  &&
	grep "flux-run: ERROR" err4.err
'
test_done
