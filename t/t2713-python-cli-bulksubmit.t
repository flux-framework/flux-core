#!/bin/sh

test_description='flux bulksubmit specific tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
test_under_flux 4 job -Slog-stderr-level=1

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py -f asciicast --line-buffer"

test_expect_success 'flux bulksubmit submits each arg as a job' '
	seq 1 3 | flux bulksubmit echo {} >bs.ids &&
	test $(wc -l < bs.ids) -eq 3 &&
	for job in $(cat bs.ids); do
	    flux job attach $job
	done | sort >bs.output &&
	cat <<-EOF >bs.expected &&
	1
	2
	3
	EOF
	test_cmp bs.expected bs.output
'
test_expect_success 'flux bulksubmit uses default command of {}' '
	flux bulksubmit --dry-run ::: foo bar baz >default-cmd.out &&
	cat <<-EOF >default-cmd.expected &&
	bulksubmit: submit foo
	bulksubmit: submit bar
	bulksubmit: submit baz
	EOF
	test_cmp default-cmd.expected default-cmd.out
'
test_expect_success 'flux bulksubmit --cc works' '
	seq 1 3 | flux bulksubmit --cc=0-1 echo {} >bs2.ids &&
	test $(wc -l < bs2.ids) -eq 6 &&
	for job in $(cat bs2.ids); do
	    flux job attach $job
	done | sort >bs2.output &&
	cat <<-EOF >bs2.expected &&
	1
	1
	2
	2
	3
	3
	EOF
	test_cmp bs2.expected bs2.output
'
test_expect_success 'flux bulksubmit --bcc works' '
	seq 1 3 | \
	    flux bulksubmit --quiet --watch --bcc=0-1 \
	        sh -c "echo {}\$FLUX_JOB_CC" >bcc.out &&
	sort bcc.out > bcc.out.sorted &&
	cat <<-EOF >bcc.expected &&
	1
	1
	2
	2
	3
	3
	EOF
	test_cmp bcc.expected bcc.out.sorted
'
test_expect_success 'flux bulksubmit --watch works' '
	flux bulksubmit --watch echo {} ::: foo bar baz >bs3.out &&
	test_debug "cat bs3.out" &&
	grep ^foo bs3.out &&
	grep ^bar bs3.out &&
	grep ^baz bs3.out
'
test_expect_success 'flux bulksubmit reports exceptions with --watch' '
	test_expect_code 1 \
	    flux bulksubmit --watch -n {} hostname ::: 1 2 4 1024 \
	    >b4.out 2>&1 &&
	test_debug "cat b4.out" &&
	grep "unsatisfiable request" b4.out
'
test_expect_success 'flux submit --progress works without --wait' '
	$runpty flux submit --cc=1-10 --progress hostname >bs5.out &&
	test_debug "cat bs5.out" &&
	grep "10 jobs" bs5.out &&
	grep "100.0%" bs5.out
'
test_expect_success 'flux submit --progress works with --wait' '
	$runpty flux submit --cc=1-10 --progress --wait hostname \
	    >bs6.out &&
	grep "PD:1 *R:0 *CD:0 *F:0" bs6.out &&
	grep "PD:0 *R:0 *CD:10 *F:0" bs6.out &&
	grep "100.0%" bs6.out
'
test_expect_success 'flux bulksubmit --wait/progress with failed jobs' '
        test_expect_code 1 $runpty flux bulksubmit \
	    -n{} --progress --wait hostname ::: 1 2 4 1024 \
	    >bs7.out &&
	grep "PD:1 *R:0 *CD:0 *F:0" bs7.out &&
	grep "PD:0 *R:0 *CD:3 *F:1" bs7.out &&
	grep "100.0%" bs7.out
'
test_expect_success 'flux bulksubmit --wait/progress with failed jobs' '
        test_expect_code 2 $runpty flux bulksubmit \
	    -n1 --progress --wait sh -c "exit {}" ::: 0 1 0 0 2 \
	    >bs8.out &&
	grep "PD:1 *R:0 *CD:0 *F:0" bs8.out &&
	grep "PD:0 *R:0 *CD:3 *F:2" bs8.out &&
	grep "100.0%" bs8.out
'
test_expect_success 'flux submit --wait/progress with job exceptions' '
        test_expect_code 1 $runpty flux bulksubmit \
	    -n1 --progress --wait \
	    --setattr=system.exec.bulkexec.mock_exception={} hostname \
	    ::: none init init none none \
	    >bs9.out &&
	grep "PD:1 *R:0 *CD:0 *F:0" bs9.out &&
	grep "PD:0 *R:0 *CD:3 *F:2" bs9.out &&
	grep "100.0%" bs9.out
'
test_expect_success 'flux bulksubmit --wait returns highest exit code' '
	test_expect_code 143 \
	    flux bulksubmit --wait sh -c "kill -{} \$\$" ::: 0 0 15
'
test_expect_success 'flux bulksubmit --wait-event works' '
	flux bulksubmit -vvv \
		--wait-event={} \
		--log-stderr=wait.{}.out \
		true ::: start exec.shell.init &&
	test_debug "cat wait.start.out" &&
	test_debug "cat wait.exec.shell.init.out" &&
	tail -n1 wait.start.out | grep start &&
	tail -n1 wait.exec.shell.init.out | grep shell.init
'
test_expect_success 'flux bulksubmit replacement format strings work' '
	echo /a/b/c/d.txt /a/b/c/d c.txt | \
	    flux bulksubmit --sep=None --dry-run \
	        {seq} {seq1} {} {.%} {./} {.//} {./%} \
	        >repl.out &&
	cat <<-EOF >repl.expected &&
	bulksubmit: submit 0 1 /a/b/c/d.txt /a/b/c/d d.txt /a/b/c d
	bulksubmit: submit 1 2 /a/b/c/d /a/b/c/d d /a/b/c d
	bulksubmit: submit 2 3 c.txt c c.txt  c
	EOF
	test_debug "cat repl.out" &&
	test_cmp repl.expected repl.out
'
test_expect_success 'flux bulksubmit can use replacement string in opts' '
	flux bulksubmit --dry-run -n {} -N {} -c {} -g {} hostname \
	    ::: 1 2 3 4 >repl2.out &&
	test_debug "cat -v repl2.out" &&
	cat <<-EOF >repl2.expected &&
	bulksubmit: submit -n1 -N1 -c1 -g1 hostname
	bulksubmit: submit -n2 -N2 -c2 -g2 hostname
	bulksubmit: submit -n3 -N3 -c3 -g3 hostname
	bulksubmit: submit -n4 -N4 -c4 -g4 hostname
	EOF
	test_cmp repl2.expected repl2.out
'
test_expect_success 'flux bulksubmit combines all inputs by default' '
	flux bulksubmit --dry-run sleep {0}.{1} \
	    ::: 0 1 2 3 4 ::: 0 5 9 >repl3.out &&
	test_debug "cat repl3.out" &&
	cat <<-EOF >repl3.expected &&
	bulksubmit: submit sleep 0.0
	bulksubmit: submit sleep 0.5
	bulksubmit: submit sleep 0.9
	bulksubmit: submit sleep 1.0
	bulksubmit: submit sleep 1.5
	bulksubmit: submit sleep 1.9
	bulksubmit: submit sleep 2.0
	bulksubmit: submit sleep 2.5
	bulksubmit: submit sleep 2.9
	bulksubmit: submit sleep 3.0
	bulksubmit: submit sleep 3.5
	bulksubmit: submit sleep 3.9
	bulksubmit: submit sleep 4.0
	bulksubmit: submit sleep 4.5
	bulksubmit: submit sleep 4.9
	EOF
	test_cmp repl3.expected repl3.out
'
test_expect_success 'flux bulksubmit links inputs with :::+' '
	flux bulksubmit --dry-run {0}:{1} \
	    ::: 0 1 2 3 4 4 :::+ a b c >linked.out &&
	test_debug "cat linked.out" &&
	cat <<-EOF >linked.expected &&
	bulksubmit: submit 0:a
	bulksubmit: submit 1:b
	bulksubmit: submit 2:c
	bulksubmit: submit 3:a
	bulksubmit: submit 4:b
	bulksubmit: submit 4:c
	EOF
	test_cmp linked.expected linked.out
'
test_expect_success 'flux bulksubmit reads from files with ::::' '
	seq 1 3 >input &&
	flux bulksubmit --dry-run :::: input >fileinput.out &&
	cat <<-EOF >fileinput.expected &&
	bulksubmit: submit 1
	bulksubmit: submit 2
	bulksubmit: submit 3
	EOF
	test_cmp fileinput.expected fileinput.out
'
test_expect_success 'flux bulksubmit splits files on newline only' '
	cat <<-EOF >inputs2 &&
	this is a line
	another line
	EOF
	flux bulksubmit --dry-run echo {} :::: inputs2 >whitespace.out &&
	cat <<-EOT >whitespace.expected &&
	bulksubmit: submit echo this is a line
	bulksubmit: submit echo another line
	EOT
	test_cmp whitespace.expected whitespace.out
'
test_expect_success 'flux bulksubmit can substitute in list options' "
	flux bulksubmit --dry-run --env=-* --env=SEQ={seq} ::: a b c \
	    >envsub.out &&
	test_debug 'cat envsub.out' &&
	cat <<-EOF >envsub.expected &&
	bulksubmit: submit --env=['-*', 'SEQ=0'] a
	bulksubmit: submit --env=['-*', 'SEQ=1'] b
	bulksubmit: submit --env=['-*', 'SEQ=2'] c
	EOF
	test_cmp envsub.expected envsub.out
"
test_expect_success 'flux bulksubmit --define works' '
 	flux bulksubmit --dry-run \
	    --define=p2="2**int(x)" \
	    --job-name={} -n {.p2} hostname ::: $(seq 1 8) \
	    >define.out &&
	test_debug "cat define.out" &&
	cat <<-EOF >define.expected &&
	bulksubmit: submit -n2 --job-name=1 hostname
	bulksubmit: submit -n4 --job-name=2 hostname
	bulksubmit: submit -n8 --job-name=3 hostname
	bulksubmit: submit -n16 --job-name=4 hostname
	bulksubmit: submit -n32 --job-name=5 hostname
	bulksubmit: submit -n64 --job-name=6 hostname
	bulksubmit: submit -n128 --job-name=7 hostname
	bulksubmit: submit -n256 --job-name=8 hostname
	EOF
	test_cmp define.expected define.out
'
test_expect_success 'flux bulksubmit --shuffle works' '
	flux bulksubmit --dry-run --shuffle \
	    {seq}:{} ::: $(seq 1 8) >shuffle.out &&
	test_debug "cat shuffle.out" &&
	sort shuffle.out > shuffle.sorted &&
	test_must_fail test_cmp shuffle.sorted shuffle.out >/dev/null
'
test_expect_success 'flux bulksubmit --progress is ignored for no tty' '
	flux submit --progress --wait --cc=0-1 sleep 0 >notty.out 2>&1 &&
	grep "Ignoring --progress option" notty.out
'
test_expect_success 'flux bulksubmit reports invalid replacement strings' '
	test_expect_code 1 flux bulksubmit {1} ::: 0 1 2 >bad.out 2>&1 &&
	test_debug "cat bad.out" &&
	test_expect_code 1 flux bulksubmit -n {1} {} ::: 0 1 2 \
	    >bad2.out 2>&1 &&
	test_debug "cat bad2.out" &&
	test_expect_code 1 flux bulksubmit -n {f} true ::: 0 1 2 \
	    >bad3.out 2>&1 &&
	test_debug "cat bad3.out" &&
	grep "Invalid replacement .* in command" bad.out &&
	grep "Invalid replacement .* in -n" bad2.out &&
	grep "Replacement key '\''f'\'' not found" bad3.out
'
test_expect_success 'flux bulksubmit: preserves mustache templates' '
	flux bulksubmit --dry-run --output=flux-{}.{{id}}.out \
	    hostname ::: 0 1 >mustache.out &&
	test_debug "cat mustache.out" &&
	cat <<-EOF > mustache.expected  &&
	bulksubmit: submit --output=flux-0.{{id}}.out hostname
	bulksubmit: submit --output=flux-1.{{id}}.out hostname
	EOF
	test_cmp mustache.expected mustache.out
'
test_expect_success 'flux bulksubmit: preserves mustache templates in command' '
	flux bulksubmit --dry-run echo {{tmpdir}} ::: 0 1 >mustache-cmd.out &&
	test_debug "cat mustache-cmd.out" &&
	cat <<-EOF > mustache-cmd.expected  &&
	bulksubmit: submit echo {{tmpdir}}
	bulksubmit: submit echo {{tmpdir}}
	EOF
	test_cmp mustache-cmd.expected mustache-cmd.out
'
test_expect_success 'flux submit --log works and can substitute {cc}' '
	flux submit --log=job{cc}.id --cc=1-5 hostname &&
	for id in $(seq 1 5); do
	    test -f job${id}.id &&
	    flux job wait-event -v $(cat job${id}.id) clean
	done
'
test_expect_success 'flux submit --log-stderr works' '
	flux submit --log-stderr=job{cc}.stderr --cc=0-1 --watch -vv \
	    hostname &&
	for id in $(seq 0 1); do
	    test -f job${id}.stderr &&
	    grep complete job${id}.stderr
	done
'
test_expect_success 'flux submit --log-stderr works' '
	flux submit --log-stderr=job{cc}.stderr --cc=0-1 --watch -vv \
	    hostname &&
	for id in $(seq 0 1); do
	    test -f job${id}.stderr &&
	    grep complete job${id}.stderr
	done
'
test_expect_success 'flux bulksubmit preserves {cc} in args' '
	flux bulksubmit --log=preserve-{cc}.out --cc=0-1 --watch \
		echo {cc}={} ::: a b c &&
	for id in 0 1; do
	    grep ^${id}=a preserve-${id}.out &&
	    grep ^${id}=b preserve-${id}.out &&
	    grep ^${id}=c preserve-${id}.out
	done
'
test_expect_success 'flux bulksubmit --dry-run works with --cc' '
	flux bulksubmit --dry-run --cc=1-2 echo {} ::: 1 2 3 \
	  >dry-run-cc.out 2>&1 &&
	cat <<-EOF >dry-run-cc.expected &&
	bulksubmit: submit --cc=1-2 echo 1
	bulksubmit: submit --cc=1-2 echo 2
	bulksubmit: submit --cc=1-2 echo 3
	EOF
	test_cmp dry-run-cc.expected dry-run-cc.out
'
test_done
