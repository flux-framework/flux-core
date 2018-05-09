#!/bin/sh
#

test_description='Test basic wreck epilog functionality
'
. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE} wreck

#  Return previous job path in kvs
last_job_path() {
	flux wreck last-jobid -p
}

epilog_path="$(pwd)/epilog.wait.sh"
kvswait=${SHARNESS_TEST_SRCDIR}/scripts/kvs-watch-until.lua
eventtrace=${SHARNESS_TEST_SRCDIR}/scripts/event-trace.lua

# Create epilog test that will block until an 'epilog.test' event
cat <<EOF >${epilog_path}
#!/bin/sh
flux event sub -c 1 epilog.test
flux event pub epilog.test.done
EOF
chmod +x ${epilog_path}

wait_for_complete() {
	$kvswait -vt 5 $1.state 'v == "complete"'
}

test_expect_success 'flux-wreck: global epilog.pre' '
	flux kvs put --json lwj.epilog.pre="$epilog_path" &&
	flux wreckrun /bin/true &&
	LWJ=$(last_job_path) &&
	STATE=$(flux kvs get --json ${LWJ}.state) &&
	test_debug "echo job state is now ${STATE}" &&
	test "$STATE" = "completing" &&
	flux event pub epilog.test &&
	wait_for_complete $LWJ
'
test_expect_success 'flux-wreck: per-job epilog.pre' '
	flux kvs unlink lwj.epilog.pre &&
	flux wreckrun -x ${epilog_path} /bin/true &&
	LWJ=$(last_job_path) &&
	test $(flux kvs get --json ${LWJ}.epilog.pre) = "$epilog_path" &&
	STATE=$(flux kvs get --json ${LWJ}.state) &&
	test_debug "echo job state is now ${STATE}" &&
	test "$STATE" = "completing" &&
	flux event pub epilog.test &&
	wait_for_complete $LWJ
'
test_expect_success 'flux-wreck: global epilog.post' '
	flux kvs put --json lwj.epilog.post="$epilog_path" &&
	flux wreckrun /bin/true &&
	wait_for_complete $LWJ &&
	${eventtrace} -t 5 epilog.test epilog.test.done \
                           flux event pub epilog.test
'
test_expect_success 'flux-wreck: per-job epilog.post' '
	flux kvs unlink lwj.epilog.post &&
	flux wreckrun -p "$epilog_path" /bin/true &&
	wait_for_complete $LWJ &&
	${eventtrace} -t 5 epilog.test epilog.test.done \
                           flux event pub epilog.test
'

test_done
