#!/bin/sh

test_description='Test resource module forced rediscovery'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if which hwloc-bind > /dev/null; then
	NCORES=$(hwloc-bind --get | hwloc-calc --number-of core | tail -n 1)
	test $NCORES = 1 || test_set_prereq MULTICORE
fi

if ! test_have_prereq MULTICORE; then
   skip_all='skipping rediscovery testing without MULTICORE'
   test_done
fi

test_under_flux 2

test_expect_success 'resource: resource.rediscover forces hwloc discovery' '
	NCORES=$(flux alloc -n1 -o cpu-affinity=off \
	         flux resource list -no {ncores}) &&
	test $NCORES -eq 1 &&
	NCORES2=$(flux alloc -n1 -o cpu-affinity=off \
	          --conf=resource.rediscover=true \
	          flux resource list -no {ncores}) &&
	test_debug "echo got ncores=$NCORES2 with resource.rediscover" &&
	test $NCORES2 -gt $NCORES
'
test_expect_success 'resource: resource.rediscover works with multiple nodes' '
	NCORES=$(flux alloc -N2 -n2 -o cpu-affinity=off \
	         flux resource list -no {ncores}) &&
	test $NCORES -eq 2 &&
	NCORES3=$(flux alloc -N2 -n2 -o cpu-affinity=off \
	          --conf=resource.rediscover=true \
	          flux resource list -no {ncores}) &&
	test_debug "echo got ncores=$NCORES3 with rediscover on 2 nodes" &&
	test $NCORES3 -gt $NCORES &&
	test $NCORES3 -eq $(($NCORES2*2))
'
test_expect_success 'resource: resource.rediscover saves expiration' '
	flux alloc -t 5m -n1 -o cpu-affinity=off \
	        --conf=resource.rediscover=true \
		flux run flux job timeleft > timeleft.out &&
	test_debug "cat timeleft.out" &&
	test $(cat timeleft.out) -lt 800
'
# Note: 4294967295 is 32bit UINT_MAX. `flux job timeleft` should report
# this value (or larger) for an unlimited expiration.
test_expect_success 'resource: resource.rediscover works with unlimited expiration' '
	flux alloc -n1 -o cpu-affinity=off \
	        --conf=resource.rediscover=true \
		flux run flux job timeleft > timeleft2.out &&
	test_debug "cat timeleft2.out" &&
	test $(cat timeleft2.out) -ge 4294967295
'

test_done
