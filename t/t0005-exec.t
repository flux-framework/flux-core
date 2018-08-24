#!/bin/sh
#

test_description='Test broker exec functionality, used by later tests


Test exec functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

invalid_rank() {
       echo $((${SIZE} + 1))
}

test_expect_success 'basic exec functionality' '
	flux exec /bin/true
'

test_expect_success 'exec to specific rank' '
	flux exec -r 0 /bin/true
'

test_expect_success 'exec to "all" ranks' '
	flux exec -r all /bin/true
'
test_expect_success 'exec to non-existent rank is an error' '
	test_must_fail flux exec -r $(invalid_rank) /bin/true
'

test_expect_success 'exec to valid and invalid ranks works' '
        # But, flux-exec should return failure:
	! flux exec -r 0,$(invalid_rank) echo working 1>stdout 2>stderr </dev/null &&
	count1=$(grep -c working stdout) &&
	count2=$(grep -c "No route to host" stderr) &&
	test "$count1" = "1" &&
	test "$count2" = "1"
'

test_expect_success 'test_on_rank works' '
	test_on_rank 1 /bin/true
'

test_expect_success 'test_on_rank sends to correct rank' '
	flux comms info | grep rank=0 && 
	test_on_rank 1 sh -c "flux comms info | grep -q rank=1"
'

test_expect_success 'test_on_rank works with test_must_fail' '
	test_must_fail test_on_rank 1 sh -c "flux comms info | grep -q rank=0"
'

test_expect_success 'flux exec passes environment variables' '
	test_must_fail flux exec -r 0 sh -c "test \"\$FOOTEST\" = \"t\"" &&
	FOOTEST=t &&
	export FOOTEST &&
	flux exec -r 0 sh -c "test \"\$FOOTEST\" = \"t\"" &&
	test_on_rank 0 sh -c "test \"\$FOOTEST\" = \"t\""
'

test_expect_success 'flux exec does not pass FLUX_URI' '
        # Ensure FLUX_URI for rank 1 doesn not equal FLUX_URI for 0
	flux exec -r 1 sh -c "test \"\$FLUX_URI\" != \"$FLUX_URI\""
'

test_expect_success 'flux exec passes cwd' '
	(cd /tmp &&
	flux exec sh -c "test \`pwd\` = \"/tmp\"")
'

test_expect_success 'flux exec -d option works' '
	flux exec -d /tmp sh -c "test \`pwd\` = \"/tmp\""
'

# Run a script on ranks 0-3 simultaneously with each rank writing the
#  rank id to a file. After successful completion, the contents of the files
#  are verfied to ensure each rank connected to the right broker.
test_expect_success 'test_on_rank works on multiple ranks' '
	ouput_dir=$(pwd) &&
	rm -f rank_output.* &&
	cat >multiple_rank_test <<EOF &&
rank=\`flux comms info | grep rank | sed s/rank=//\`
echo \$rank > $(pwd)/rank_output.\${rank}
exit 0
EOF
	test_on_rank 0-3 sh $(pwd)/multiple_rank_test &&
	test `cat rank_output.0`  = "0" &&
	test `cat rank_output.1`  = "1" &&
	test `cat rank_output.2`  = "2" &&
	test `cat rank_output.3`  = "3"
'

test_expect_success 'flux exec exits with code 127 for file not found' '
	test_expect_code 127 run_timeout 2 flux exec ./nosuchprocess
'

test_expect_success 'flux exec exits with code 126 for non executable' '
	test_expect_code 126 flux exec /dev/null
'

test_expect_success 'flux exec exits with code 68 (EX_NOHOST) for rank not found' '
	test_expect_code 68 run_timeout 2 flux exec -r 1000 ./nosuchprocess
'
test_expect_success 'flux exec passes non-zero exit status' '
	test_expect_code 2 flux exec sh -c "exit 2" &&
	test_expect_code 3 flux exec sh -c "exit 3" &&
	test_expect_code 139 flux exec sh -c "kill -11 \$\$"
'

test_expect_success 'basic IO testing' '
	flux exec -r0 echo Hello | grep ^Hello\$  &&
	flux exec -r0 sh -c "echo Hello >&2" 2>stderr &&
	cat stderr | grep ^Hello\$
'

test_expect_success 'per rank output works' '
	flux exec -r 1 sh -c "flux comms info | grep rank" | grep ^rank=1\$ &&
	flux exec -lr 2 sh -c "flux comms info | grep rank" | grep ^2:\ rank=2\$ &&
	cat >expected <<EOF &&
0: rank=0
1: rank=1
2: rank=2
3: rank=3
EOF
	flux exec -lr 0-3 sh -c "flux comms info | grep rank" | sort -n >output &&
	test_cmp output expected
'

test_expect_success 'I/O, multiple lines, no newline on last line' '
	/bin/echo -en "1: one\n1: two" >expected &&
	flux exec -lr 1 /bin/echo -en "one\ntwo" >output &&
	test_cmp output expected &&
	/bin/echo -en "1: one" >expected &&
	flux exec -lr 1 /bin/echo -en "one" >output &&
	test_cmp output expected
'

test_expect_success 'I/O -- long lines' '
	dd if=/dev/urandom bs=4096 count=1 | base64 --wrap=0 >expected &&
	flux exec -r1 cat expected > output &&
	test_cmp output expected
'


test_expect_success 'signal forwarding works' '
	cat >test_signal.sh <<-EOF &&
	#!/bin/bash
	sig=\${1-INT}
	flux exec sleep 100 </dev/null &
	sleep 1 &&
	kill -\$sig %1 &&
	wait %1
	exit \$?
	EOF
	chmod +x test_signal.sh &&
	test_expect_code 130 run_timeout 5 ./test_signal.sh INT &&
	test_expect_code 143 run_timeout 5 ./test_signal.sh TERM
'

test_expect_success 'flux-exec: stdin bcast' '
	count=$(echo Hello | flux exec -r0-3 cat | grep -c Hello) &&
	test "$count" = "4"
'

test_expect_success 'stdin redirect from /dev/null works' '
	test_expect_code 0 run_timeout 1 flux exec -r0-3 cat
'

test_expect_success 'stdin redirect from /dev/null works via -n' '
       test_expect_code 0 run_timeout 1 flux exec -n -r0-3 cat
'

test_expect_success 'stdin broadcast -- multiple lines' '
	dd if=/dev/urandom bs=1024 count=4 | base64 >expected &&
	cat expected | run_timeout 3 flux exec -l -r0-3 cat >output &&
	for i in $(seq 0 3); do
		sed -n "s/^$i: //p" output > output.$i &&
		test_cmp expected output.$i
	done
'

test_expect_success 'stdin broadcast -- long lines' '
	dd if=/dev/urandom bs=1024 count=4 | base64 --wrap=0 >expected &&
        echo >>expected &&
	cat expected | run_timeout 3 flux exec -l -r0-3 cat >output &&
	for i in $(seq 0 3); do
		sed -n "s/^$i: //p" output > output.$i
		test_cmp expected output.$i
	done
'

test_done
