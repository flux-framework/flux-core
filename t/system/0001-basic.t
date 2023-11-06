#
#  Basic system instance sanity checks
#
test_expect_success 'system instance runs job as current uid' '
	jobid=$(flux submit id -u) &&
	result=$(flux job attach $jobid) &&
	test_debug "echo Job ran with userid $result" &&
	test $result -eq $(id -u) &&
	test $(flux getattr security.owner) -ne $result
'
test_expect_success 'flux jobs lists job with correct userid' '
	test $(flux jobs -no {userid} $jobid) -eq $(id -u)
'
# flux-framework/flux-core#5530
test_expect_success 'flux proxy can submit jobs to system instance' '
	flux proxy $(flux getattr local-uri) flux submit true
'
