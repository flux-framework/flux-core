#!/bin/sh
#

test_description='Test broker state machine'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

RPC=${FLUX_BUILD_DIR}/t/request/rpc
SRPC=${FLUX_BUILD_DIR}/t/request/rpc_stream
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="
GROUPSCMD="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

test_expect_success 'quorum reached on instance with 1 TBON level' '
	echo "0-2" >full1.exp &&
	flux start -s3 ${ARGS} ${GROUPSCMD} get broker.online >full1.out &&
	test_cmp full1.exp full1.out
'

test_expect_success 'quorum reached on instance with 2 TBON levels' '
	echo "0-3" >full2.exp &&
	flux start -s4 ${ARGS} ${GROUPSCMD} get broker.online >full2.out &&
	test_cmp full2.exp full2.out
'

test_expect_success 'quorum reached on instance with 3 TBON levels' '
	echo "0-7" >full3.exp &&
	flux start -s8 ${ARGS} ${GROUPSCMD} get broker.online >full3.out &&
	test_cmp full3.exp full3.out
'

test_expect_success 'broker.quorum can be set on the command line' '
	flux start -s3 ${ARGS} -Sbroker.quorum=3 \
		${GROUPSCMD} get broker.online >full1_explicit.out &&
	test_cmp full1.exp full1_explicit.out
'

test_expect_success 'broker fails with malformed broker.quorum' '
	test_must_fail flux start ${ARGS} \
		-Sbroker.quorum=9-10 true 2>qmalformed.err &&
	grep "Error parsing broker.quorum attribute" qmalformed.err
'

test_expect_success 'broker fails with broker.quorum that exceeds size' '
	test_must_fail flux start ${ARGS} \
		-Sbroker.quorum=99 true 2>qtoobig.err &&
	grep "Error parsing broker.quorum attribute" qtoobig.err
'
test_expect_success 'broker.quorum can be 0 for compatibility' '
	flux start ${ARGS} -Sbroker.quorum=0 true 2>compat1.err &&
	grep assuming compat1.err
'
test_expect_success 'broker.quorum can be 0-1 (size=2) for compatibility' '
	flux start -s2 ${ARGS} -Sbroker.quorum=0-1 true 2>compat2.err &&
	grep assuming compat2.err
'
test_expect_success 'create rc1 that blocks on FIFO for rank != 0' '
	cat <<-EOT >rc1_block &&
	#!/bin/bash
	rank=\$(flux getattr rank)
	test \$rank -eq 0 || cat fifo
	EOT
	chmod +x rc1_block
'

test_expect_success 'create rc2 that unblocks FIFO' '
	cat <<-EOT >rc2_unblock &&
	#!/bin/bash
	${GROUPSCMD} get broker.online
	echo UNBLOCKED! >>fifo
	EOT
	chmod +x rc2_unblock
'

# Delay rank 1 so that we can check that initial program ran with only
# rank 0 in RUN state.
test_expect_success 'instance functions with late-joiner' '
	echo "0" >late.exp &&
	rm -f fifo &&
	mkfifo fifo &&
	run_timeout 60 \
		flux start -s2 \
		-Slog-stderr-level=6 \
		-Sbroker.rc1_path="$(pwd)/rc1_block" \
		-Sbroker.rc3_path= \
		-Sbroker.quorum=1 \
		$(pwd)/rc2_unblock >late.out &&
	test_cmp late.exp late.out
'

test_expect_success 'quorum-get RPC fails on rank > 0' '
	test_must_fail flux start -s2 ${ARGS} \
		flux exec -r1 ${GROUPSCMD} get --rank 1 broker.online \
		</dev/null 2>qm1.err &&
	grep "only available on rank 0" qm1.err
'

test_expect_success 'monitor streaming RPC works' '
	flux start ${ARGS} \
		$SRPC state-machine.monitor state \
		</dev/null >state.out &&
	jq -cea .state state.out
'

test_expect_success 'create rc script that prints current state' '
	cat <<-EOT >rc_getstate &&
	#!/bin/bash
	$RPC state-machine.monitor </dev/null | jq -cea .state >rc.out
	EOT
	chmod +x rc_getstate
'

test_expect_success 'monitor reports INIT(2) in rc1' '
	echo 2 >rc1.exp &&
	flux start \
		-Sbroker.rc1_path=$(pwd)/rc_getstate \
		-Sbroker.rc3_path= \
		true &&
	test_cmp rc1.exp rc.out
'

test_expect_success 'monitor reports RUN(4) in rc2' '
	echo 4 >rc2.exp &&
	flux start \
		-Sbroker.rc1_path= \
		-Sbroker.rc3_path= \
		$(pwd)/rc_getstate &&
	test_cmp rc2.exp rc.out
'

test_expect_success 'monitor reports CLEANUP(5) in cleanup script' '
	echo 5 >cleanup.exp &&
	flux start \
		-Sbroker.rc1_path= \
		-Sbroker.rc3_path= \
		bash -c "echo $(pwd)/rc_getstate | flux admin cleanup-push" &&
	test_cmp cleanup.exp rc.out
'

test_expect_success 'monitor reports FINALIZE(7) in rc3' '
	echo 7 >rc3.exp &&
	flux start \
		-Sbroker.rc1_path= \
		-Sbroker.rc3_path=$(pwd)/rc_getstate \
		true &&
	test_cmp rc3.exp rc.out
'

test_expect_success 'capture state transitions from size=1 instance' '
	flux start ${ARGS} -Slog-filename=states.log true
'

test_expect_success 'all expected events and state transitions occurred' '
	grep "builtins-success: none->join"		states.log &&
	grep "parent-none: join->init"			states.log &&
	grep "rc1-none: init->quorum"			states.log &&
	grep "quorum-full: quorum->run"			states.log &&
	grep "rc2-success: run->cleanup"		states.log &&
	grep "cleanup-none: cleanup->shutdown"		states.log &&
	grep "children-none: shutdown->finalize"	states.log &&
	grep "rc3-none: finalize->goodbye"		states.log &&
	grep "goodbye: goodbye->exit"			states.log
'

test_expect_success 'capture state transitions from size=2 instance' '
	flux start ${ARGS} --test-size=2 \
		-Slog-stderr-level=6 -Slog-stderr-mode=local \
		true 2>states2.log
'

test_expect_success 'all expected events and state transitions occurred on rank 0' '
	grep "\[0\]: builtins-success: none->join"		states2.log &&
	grep "\[0\]: parent-none: join->init"			states2.log &&
	grep "\[0\]: rc1-none: init->quorum"			states2.log &&
	grep "\[0\]: quorum-full: quorum->run"			states2.log &&
	grep "\[0\]: rc2-success: run->cleanup"			states2.log &&
	grep "\[0\]: cleanup-none: cleanup->shutdown"		states2.log &&
	grep "\[0\]: children-complete: shutdown->finalize"	states2.log &&
	grep "\[0\]: rc3-none: finalize->goodbye"		states2.log &&
	grep "\[0\]: goodbye: goodbye->exit"			states2.log
'

test_expect_success 'all expected events and state transitions occurred on rank 1' '
	grep "\[1\]: builtins-success: none->join"		states2.log &&
	grep "\[1\]: parent-ready: join->init"			states2.log &&
	grep "\[1\]: rc1-none: init->quorum"			states2.log &&
	grep "\[1\]: quorum-full: quorum->run"			states2.log &&
	grep "\[1\]: shutdown: run->cleanup"			states2.log &&
	grep "\[1\]: cleanup-none: cleanup->shutdown"		states2.log &&
	grep "\[1\]: children-none: shutdown->finalize"	        states2.log &&
	grep "\[1\]: rc3-none: finalize->goodbye"		states2.log &&
	grep "\[1\]: goodbye: goodbye->exit"		        states2.log
'

test_expect_success 'capture state transitions from instance with rc1 failure' '
	test_expect_code 1 flux start \
	    -Slog-filename=states_rc1.log \
	    -Sbroker.rc1_path=false \
	    -Sbroker.rc3_path= \
	    true
'

test_expect_success 'all expected events and state transitions occurred' '
	grep "builtins-success: none->join"		states_rc1.log &&
	grep "parent-none: join->init"			states_rc1.log &&
	grep "rc1-fail: init->shutdown"			states_rc1.log &&
	grep "children-none: shutdown->finalize"	states_rc1.log &&
	grep "rc3-none: finalize->goodbye"		states_rc1.log &&
	grep "goodbye: goodbye->exit"			states_rc1.log
'

test_expect_success 'capture state transitions from instance with rc2 failure' '
	test_expect_code 1 flux start \
	    -Slog-filename=states_rc2.log \
	    ${ARGS} \
	    false
'

test_expect_success 'all expected events and state transitions occurred' '
	grep "builtins-success: none->join"		states_rc2.log &&
	grep "parent-none: join->init"			states_rc2.log &&
	grep "rc1-none: init->quorum"			states_rc2.log &&
	grep "quorum-full: quorum->run"			states_rc2.log &&
	grep "rc2-fail: run->cleanup"		        states_rc2.log &&
	grep "cleanup-none: cleanup->shutdown"		states_rc2.log &&
	grep "children-none: shutdown->finalize"	states_rc2.log &&
	grep "rc3-none: finalize->goodbye"		states_rc2.log &&
	grep "goodbye: goodbye->exit"			states_rc2.log
'

test_expect_success 'capture state transitions from instance with rc3 failure' '
	test_expect_code 1 flux start \
	    -Slog-filename=states_rc3.log \
	    -Sbroker.rc1_path= \
	    -Sbroker.rc3_path=false \
	    true
'

test_expect_success 'all expected events and state transitions occurred' '
	grep "builtins-success: none->join"		states_rc3.log &&
	grep "parent-none: join->init"			states_rc3.log &&
	grep "rc1-none: init->quorum"			states_rc3.log &&
	grep "quorum-full: quorum->run"			states_rc3.log &&
	grep "rc2-success: run->cleanup"		states_rc3.log &&
	grep "cleanup-none: cleanup->shutdown"		states_rc3.log &&
	grep "children-none: shutdown->finalize"	states_rc3.log &&
	grep "rc3-fail: finalize->goodbye"		states_rc3.log &&
	grep "goodbye: goodbye->exit"			states_rc3.log
'

test_expect_success 'instance rc1 failure exits with norestart code' '
	test_expect_code 99 flux start \
	    -Sbroker.exit-norestart=99 \
	    -Sbroker.rc1_path=false \
	    -Sbroker.rc3_path= \
	    true
'

test_expect_success 'broker.quorum-warn=none is accepted' '
	flux start ${ARGS} -Sbroker.quorum-warn=none true
'

test_expect_success 'broker.quorum-warn=3h is accepted' '
	flux start ${ARGS} -Sbroker.quorum-warn=3h true
'
test_expect_success 'broker.quorum-warn=x fails' '
	test_must_fail flux start ${ARGS} -Sbroker.quorum-warn=x true
'
test_expect_success 'create rc1 that sleeps for 2s on rank != 0' '
	cat <<-EOT >rc1_sleep &&
	#!/bin/bash
	rank=\$(flux getattr rank)
	test \$rank -eq 0 || sleep 2
	EOT
	chmod +x rc1_sleep
'
test_expect_success 'broker.quorum-warn works' '
	flux start -s2 ${ARGS} \
		-Slog-filename=timeout.log \
		-Sbroker.rc1_path="$(pwd)/rc1_sleep" \
		-Sbroker.quorum-warn=1s true
'
test_expect_success 'logs contain quorum delayed/reached messages' '
	grep "quorum delayed" timeout.log &&
	grep "quorum reached" timeout.log
'

test_expect_success 'broker.cleanup-timeout default is none' '
	flux start ${ARGS} \
		flux getattr broker.cleanup-timeout >cto.out &&
	cat >cto.exp <<-EOT &&
	none
	EOT
	test_cmp cto.exp cto.out
'

test_expect_success 'broker.cleanup-timeout=3h is accepted' '
	flux start ${ARGS} \
		-Sbroker.cleanup-timeout=3h \
		flux getattr broker.cleanup-timeout >cto2.out &&
	cat >cto2.exp <<-EOT &&
	3h
	EOT
	test_cmp cto2.exp cto2.out
'
test_expect_success 'broker.cleanup-timeout=x fails' '
	test_must_fail flux start ${ARGS} \
		-Sbroker.cleanup-timeout=x true
'
test_expect_success 'create rc1 that hangs in cleanup' '
	cat <<-EOT >rc1_cleanup &&
	#!/bin/sh
	rank=\$(flux getattr rank)
	test \$rank -eq 0 || exit 0
	echo "sleep 30" | flux admin cleanup-push
	EOT
	chmod +x rc1_cleanup
'
test_expect_success 'create initial program that SIGTERMs broker' '
	cat <<-EOT >killbroker &&
	#!/bin/sh
	# Usage: killbroker signum sleepsec
	kill -\$1 \$(flux getattr broker.pid)
	sleep \$2
	EOT
	chmod +x killbroker
'
test_expect_success 'cleanup gets SIGHUP after broker.cleanup-timeout expires' '
	test_expect_code 129 flux start -s2 ${ARGS} \
		-Slog-filename=cleanup.log \
		-Sbroker.rc1_path="$(pwd)/rc1_cleanup" \
		-Sbroker.cleanup-timeout=1s \
		./killbroker 15 60
'

test_expect_success 'create hanging rc3 for rank > 0' '
	cat <<-EOT >rc3_hang &&
	#!/bin/sh
	rank=\$(flux getattr rank)
	test \$rank -eq 0 && exit 0
	sleep 5
	EOT
	chmod +x rc3_hang
'

test_expect_success 'run instance with short shutdown warn period' '
	flux start -s3 \
		-Slog-filename=shutdown.log \
		-Sbroker.rc1_path= \
		-Sbroker.rc3_path="$(pwd)/rc3_hang" \
		-Sbroker.shutdown-warn=1s \
		true
'
test_expect_success 'appropriate message was logged' '
	grep "shutdown delayed" shutdown.log
'

test_done
