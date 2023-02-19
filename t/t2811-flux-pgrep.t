#!/bin/sh

test_description='Test the flux-pgrep/pkill commands'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

export FLUX_PYCLI_LOGLEVEL=10
export SHELL=/bin/sh

test_expect_success 'flux-pgrep errors on invalid arguments' '
	test_must_fail flux pgrep &&
	test_expect_code 2 flux pgrep \[
'
test_expect_success 'flux-pgrep returns 1 when no jobs match' '
	test_expect_code 1 flux pgrep foo
'
test_expect_success 'flux-pgrep works for job nanes' '
	flux mini bulksubmit --job-name={} sleep 60 \
		::: testA testB foo &&
	flux pgrep ^test > pgrep.out &&
	test $(wc -l <pgrep.out) -eq 2
'
test_expect_success 'flux-pgrep --format option works' '
	flux pgrep -o full ^foo >pgrep-full.out &&
	test_debug "cat pgrep-full.out" &&
	grep NAME pgrep-full.out
'
test_expect_success 'flux-pgrep --format option detects errors' '
	test_must_fail flux pgrep -o {foo} ^foo
'
test_expect_success 'flux-pgrep --filter works' '
	flux pgrep --filter=sched,run . >pgrep-filtered.out &&
	test_debug "cat pgrep-filtered.out" &&
	test $(wc -l <pgrep-filtered.out) -eq 3
'
test_expect_success 'flux-pkill works' '
	flux pkill ^test &&
	test_expect_code 1 flux pkill ^test &&
	flux pkill foo
'
test_expect_success 'flux-pgrep works with jobid ranges' '
	flux mini submit --bcc=1-6 sleep 60 >ids &&
	flux mini submit --bcc=1-6 sleep 60 >ids2 &&
	flux pgrep $(head -n 1 ids)..$(tail -n1 ids) >pgrep.ids &&
	test $(wc -l <pgrep.ids) -eq 6 &&
	flux pgrep $(head -n 1 ids)..$(tail -n1 ids2) >pgrep2.ids &&
	test $(wc -l <pgrep2.ids) -eq 12 &&
	flux pkill $(head -n1 ids)..$(tail -n1 ids) &&
	flux pgrep $(head -n 1 ids)..$(tail -n1 ids2) >pgrep3.ids &&
	test $(wc -l <pgrep3.ids) -eq 6 &&
	flux pgrep -a $(head -n 1 ids)..$(tail -n1 ids2) >pgrep4.ids &&
	test $(wc -l <pgrep4.ids) -eq 12
'
test_expect_success 'flux-pgrep assumes regex with invalid jobid(s)' '
	flux pgrep sl..p
'
test_expect_success 'flux-pkill --wait option works' '
	flux pkill --wait sleep &&
	test_expect_code 1 flux pgrep .
'
test_expect_success 'flux-pkill reports errors' '
	flux mini submit sleep 60 &&
	test_expect_code 1 flux pkill -ac 2 sleep >pkill.out 2>&1 &&
	test_debug "cat pkill.out" &&
	grep ERROR.*inactive pkill.out
'
test_expect_success 'flux-pgrep accepts jobid range and pattern' '
	flux mini bulksubmit --job-name={} sleep 60 ::: foo bar foo >ids3 &&
	id1=$(sed -n 1p ids3) &&
	id2=$(sed -n 2p ids3) &&
	id3=$(sed -n 3p ids3) &&
	flux pgrep -no full ${id1}..${id2} foo >pgrep.out3 &&
	test_debug "cat pgrep.out3" &&
	test $(wc -l < pgrep.out3) -eq 1 &&
	grep foo pgrep.out3 &&
	flux pkill --wait $id1..$id3
'
test_expect_success 'flux-pgrep forces regex with name: prefix' '
	flux mini submit --job-name=f123f234 sleep 60 &&
	flux pgrep name:f1..f2 &&
	flux pkill --wait ^f123f234
'
test_expect_success 'flux-pgrep reads flux-jobs formats configuration' '
	mkdir -p dir/flux &&
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	[formats.myformat]
	description = "my format"
	format = "{id.words}"
	EOF
	XDG_CONFIG_HOME="$(pwd)/dir" \
		flux pgrep --format=help foo >myformat.out&&
	test_debug "cat myformat.out" &&
	grep "my format" myformat.out  &&
	flux mini submit --job-name=test sleep 30 &&
	XDG_CONFIG_HOME="$(pwd)/dir" \
		flux pgrep -o myformat test &&
	flux pkill test
'
test_done
