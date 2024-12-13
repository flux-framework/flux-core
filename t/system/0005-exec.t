#
#  Check exec service with guest/owner
#

test_expect_success 'start a long-running guest job' '
        flux submit -n1 --wait-event=start sleep inf &&
	jobid=$(flux job last)
'
test_expect_success 'flux exec --jobid fails as guest' '
	test_must_fail flux exec --jobid=$jobid true
'
test_expect_success 'flux exec --jobid fails as instance owner' '
	test_must_fail sudo -u flux flux exec --jobid=$jobid true
'
test_expect_success 'flux exec without --jobid works as instance owner' '
	sudo -u flux flux exec -r 0 true
'
test_expect_success 'flux exec without --jobid fails as guest' '
	test_must_fail flux exec -r 0 true
'
test_expect_success 'cancel long-running job' '
	flux cancel $jobid
'
