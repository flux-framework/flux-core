#!/bin/sh
#

test_description='Test instance restart'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'run a job in persistent instance' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux mini run -v /bin/true 2>&1 | sed "s/jobid: //" >id1.out
'

test_expect_success 'restart instance and run another job' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux mini run -v /bin/true 2>&1 | sed "s/jobid: //" >>id2.out
'

test_expect_success 'restart instance and run another job' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux mini run -v /bin/true 2>&1 | sed "s/jobid: //" >>id3.out
'

test_expect_success 'restart instance and list inactive jobs' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux jobs --suppress-header --format={id} \
		   	--filter=INACTIVE >list.out
'

test_expect_success 'inactive job list contains all jobs run before' '
	grep $(cat id1.out) list.out &&
	grep $(cat id2.out) list.out &&
	grep $(cat id3.out) list.out
'

test_expect_success 'job IDs were issued in ascending order' '
	test $(cat id1.out) -lt $(cat id2.out) &&
	test $(cat id2.out) -lt $(cat id3.out)
'

test_done
