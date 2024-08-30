#!/bin/sh

test_description='Test flux hostlist command'

. $(dirname $0)/sharness.sh

test_under_flux 2

test_expect_success 'flux-hostlist --help works' '
	flux hostlist --help >help.out &&
	test_debug "cat help.out" &&
	grep "SOURCES may include" help.out
'
test_expect_success 'flux-hostlist returns hostlist attr in initial program' '
	flux hostlist -l >hl-instance.out &&
	flux getattr hostlist >hl-instance.expected &&
	test_cmp hl-instance.expected hl-instance.out
'
test_expect_success 'flux-hostlist reads from stdin by default' '
	echo foo[0-3] | flux hostlist
'
# Need to simulate open stdin with a fifo since sharness closes stdin
test_expect_success NO_CHAIN_LINT 'flux-hostlist includes a timeout on reading stdin' '
	rm -f input.fifo;
	mkfifo input.fifo;
	FLUX_HOSTLIST_STDIN_TIMEOUT=0.1 flux hostlist <input.fifo &
	pid=$! &&
	test_when_finished rm -f input.fifo &&
	exec 9>input.fifo &&
	test_must_fail wait $pid &&
	exec 9>&-
'
test_expect_success 'flux-hostlist returns job hostlist in job' '
	flux run flux hostlist -l >hl-job.out &&
	flux jobs -no {nodelist} $(flux job last) > hl-job.expected &&
	test_cmp hl-job.expected hl-job.out
'
test_expect_success 'flux-hostlist works with a jobid' '
	flux hostlist $(flux job last) >hl-jobid.out &&
	test_cmp hl-job.expected hl-jobid.out
'
test_expect_success 'flux-hostlist fails with invalid jobid' '
	test_must_fail flux hostlist foo1
'
test_expect_success 'flux-hostlist --fallback treats invalid jobid as host' '
	flux hostlist --fallback foo1 >fallback.out &&
	test "$(cat fallback.out)" = "foo1"
'
test_expect_success 'flux-hostlist -c works' '
	flux hostlist --count local  &&
	test $(flux hostlist --count local) -eq 2 &&
	test $(flux hostlist -c "foo[1-10]") -eq 10
'
test_expect_success 'flux-hostlist works with "avail"' '
	flux resource drain 0 &&
	test $(flux hostlist --count avail) -eq 1 &&
	flux resource undrain 0
'
test_expect_success 'flux-hostlist works with stdin' '
	printf "foo1 foo2 foo3"   | flux hostlist >hl-stdin1.out &&
	printf "foo1\nfoo2\nfoo3" | flux hostlist >hl-stdin2.out &&
	test_debug "grep . hl-stdin*.out" &&
	printf "foo[1-3]\n" >hl-stdin.expected &&
	test_cmp hl-stdin.expected hl-stdin1.out &&
	test_cmp hl-stdin.expected hl-stdin2.out
'
test_expect_success 'flux-hostlist works with hostlist args' '
	flux hostlist "foo[1-3]" > hl-args1.out &&
	flux hostlist --fallback foo1 foo2 foo3 > hl-args2.out &&
	printf "foo[1-3]\n" >hl-args.expected &&
	test_cmp hl-args.expected hl-args1.out &&
	test_cmp hl-args.expected hl-args2.out
'
test_expect_success 'flux-hostlist rejects invalid hostlist' '
	test_must_fail flux hostlist "foo[1-"
'
test_expect_success 'flux-hostlist returns empty hostlist for pending job' '
	id=$(flux submit --urgency=hold hostname) &&
	flux hostlist $id >hl-pending.out &&
	test_must_be_empty hl-pending.out
'
# Note job "$id" should still be held when this next test is run:
test_expect_success 'flux-hostlist --quiet works' '
	flux hostlist --quiet foo[1-10] > quiet.out &&
	test_must_be_empty quiet.out &&
	test_must_fail flux hostlist -q $id
'
test_expect_success 'flux-hostlist -e, --expand works' '
	test_debug "flux hostlist -e foo[1-3]" &&
	test "$(flux hostlist -e foo[1-3])" = "foo1 foo2 foo3" &&
	flux hostlist --expand --delimiter="\n" "foo[1-3]" >expand.out &&
	cat <<-EOF >expand.expected &&
	foo1
	foo2
	foo3
	EOF
	test_cmp expand.expected expand.out
'
test_expect_success 'flux-hostlist -n, --nth works' '
	test "$(flux hostlist --nth=1 foo[1-10])" = foo2 &&
	test "$(flux hostlist --nth=-1 foo[1-10])" = foo10
'
test_expect_success 'flux-hostlist -n errors with invalid index' '
	test_must_fail flux hostlist -n 10 foo[1-10]
'
test_expect_success 'flux-hostlist -L, --limit works' '
	test "$(flux hostlist -L 2 foo[1-10])" = "foo[1-2]" &&
	test "$(flux hostlist -L -2 foo[1-10])" = "foo[9-10]"
'
test_expect_success 'flux-hostlist preserves hostlist order by default' '
	test "$(flux hostlist host1 host0 host5 host4)" = "host[1,0,5,4]"
'
test_expect_success 'flux-hostlist preserves repeated hosts by default' '
	test "$(flux hostlist host1 host1 host1)" = "host[1,1,1]"
'
test_expect_success 'flux-hostlist -S, --sort works' '
	test "$(flux hostlist -S host3 host2 host1 host0)" = "host[0-3]"
'
test_expect_success 'flux-hostlist -S preserves duplicate hosts' '
	test "$(flux hostlist -S host2 host1 host3 host1)" = "host[1,1-3]"
'
test_expect_success 'flux-hostlist -u  returns union of all hosts' '
	test "$(flux hostlist -u host2 host1 host3 host1)" = "host[1-3]" &&
	test "$(flux hostlist -u host[1-3] host2)" = "host[1-3]" &&
	test "$(flux hostlist -u host[1-3] bar baz)" = "bar,baz,host[1-3]"
'
test_expect_success 'flux-hostlist -i, --intersect works' '
	test "$(flux hostlist -i host[1-3] host[2-5])" = "host[2-3]" &&
	test "$(flux hostlist -i host[1-3] host[2-5] host[3-10])" = "host3"
'
# Note symmetric difference of 3 sets is all elements that are in just one
# set or all 3 sets due to associative property.
test_expect_success 'flux-hostlist -X, --xor works' '
	flux hostlist -X host[1-3] host[2-5] &&
	test "$(flux hostlist -X host[1-3] host[2-5])" = "host[1,4-5]" &&
	flux hostlist -X host[1-3] host[2-5] host[3-10] &&
	test "$(flux hostlist -X host[1-3] host[2-5] host[3-10])" = "host[1,3,6-10]" &&
	flux hostlist -X host[3-10] host[1-3] host[2-5] &&
	test "$(flux hostlist -X host[3-10] host[1-3] host[2-5])" = "host[1,3,6-10]"
'
test_expect_success 'flux-hostlist -m, --minus works' '
	flux hostlist -m host[1-10] host[2-3] &&
	test "$(flux hostlist -m host[1-10] host[2-3])" = "host[1,4-10]" &&
	flux hostlist -m host[1-10] host[2-3] host[5-10] &&
	test "$(flux hostlist -m host[1-10] host[2-3] host[5-10])" = \
             "host[1,4]" &&
	test "$(flux hostlist -m host[1-10] host[1-20])" = "" &&
	test_expect_code 1 flux hostlist -qm host[1-10] host[1-20]
'
test_expect_success 'flux-hostlist -m, --minus removes only first occurrence' '
	flux hostlist -m host[1,1,1] host1 &&
	test "$(flux hostlist -m host[1,1,1] host1)" = "host[1,1]"
'
test_expect_success 'flux-hostlist: -u can be used with -m' '
	flux hostlist -m host[1,1,1] host1 &&
	test "$(flux hostlist -um host[1,1,1] host1)" = "host1"
'
test_expect_success 'flux-hostlist -q, --quiet works' '
	flux hostlist --quiet host[1-10] >quiet.out &&
	test_must_be_empty quiet.out &&
	test_must_fail flux hostlist --quiet --intersect host1 host2
'
test_done
