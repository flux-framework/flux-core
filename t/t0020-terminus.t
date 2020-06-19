#!/bin/sh
#

test_description='Test flux-terminus command

Verify basic functionality of flux-terminus command
'

. `dirname $0`/sharness.sh
SIZE=2
test_under_flux ${SIZE} minimal

# Unset any existing terminus session
unset FLUX_TERMINUS_SESSION

userid=$(id -u)
default_service="${userid}-terminus"
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py -f asciicast"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

test_expect_success 'flux-terminus: list reports error with no server' '
	name="list-no-server" &&
	test_expect_code 1 flux terminus list >log.${name} 2>&1 &&
	grep "no server running at ${default_service}" log.${name} &&
	test_expect_code 1 flux terminus list -r 1 >log.${name}.1 2>&1 &&
	grep "no server running at ${default_service}" log.${name}.1 &&
	test_expect_code 1 flux terminus list -s foo >log.${name}.foo 2>&1 &&
	grep "no server running at foo" log.${name}.foo
'
test_expect_success 'flux-terminus: kill reports error with no server' '
	name="kill-no-server" &&
	test_expect_code 1 flux terminus kill 0 >log.${name} 2>&1 &&
	grep "no server running at ${default_service}" log.${name}
'
test_expect_success 'flux-terminus: attach fails with error on no server' '
	test_expect_code 1 flux terminus attach 0
'
test_expect_success 'flux-terminus: kill-server reports error with no server' '
	name="kill-server-no-server" &&
	test_expect_code 1 flux terminus kill-server >log.${name} 2>&1 &&
	grep "no server running at ${default_service}" log.${name}
'
test_expect_success 'flux-terminus: list reports error with bad rank/service' '
	name="list-bad-args" &&
	test_expect_code 1 flux terminus list -r -1 >log.${name} 2>&1 &&
	grep "no server running at ${default_service}" log.${name} &&
	test_expect_code 1 flux terminus list -r 1 >log.${name}.1 2>&1 &&
	grep "no server running at ${default_service}" log.${name}.1 &&
	test_expect_code 1 flux terminus list -s foo >log.${name}.foo 2>&1 &&
	grep "no server running at foo" log.${name}.foo
'
test_expect_success 'flux-terminus: attach fails with invalid id' '
	test_expect_code 1 flux terminus attach foo >log.attach-badid 2>&1 &&
	grep "session ID must be an integer" log.attach-badid
'
test_expect_success 'flux-terminus: kill fails with invalid id' '
	test_expect_code 1 flux terminus kill foo >log.kill-badid 2>&1 &&
	grep "session ID must be an integer" log.kill-badid
'
test_expect_success 'flux-terminus: start with invalid command fails' '
	test_expect_code 1 flux terminus start /nosuchthing >log.badcmd 2>&1 &&
	grep "failed to run /nosuchthing" log.badcmd
'
test_expect_success 'flux-terminus: start detached starts server' '
	flux terminus start -d &&
	flux terminus list >log.list &&
	test_debug "cat log.list" &&
	grep -q "1 current session" log.list
'
test_expect_success 'flux-terminus: start detached starts another session' '
	flux terminus start -d &&
	flux terminus list >log.list &&
	test_debug "cat log.list" &&
	grep -q "2 current sessions" log.list
'
test_expect_success 'flux-terminus: kill session works' '
	flux terminus kill 0 &&
	flux terminus list >log.list &&
	test_debug "cat log.list" &&
	grep -q "1 current session" log.list
'
test_expect_success 'flux-terminus: start --name option works' '
	flux terminus start -d --name=test-name &&
	flux terminus list >log.list &&
	test_debug "cat log.list" &&
	grep -q test-name log.list
'
test_expect_success 'flux-terminus: kill-server works' '
	flux terminus kill-server &&
	test_expect_code 1 flux terminus list
'
test_expect_success 'flux-terminus: start server on remote rank' '
	flux exec -r 1 flux terminus start -d &&
	flux terminus list -r 1 >log.list-remote &&
	test_debug "cat log.list-remote"  &&
	grep -q "1 current session" log.list-remote
'
test_expect_success 'flux-terminus: start on remote rank' '
	flux terminus start -dr 1 &&
	flux terminus list -r 1 >log.list-remote &&
	test_debug "cat log.list-remote"  &&
	grep -q "2 current sessions" log.list-remote
'
test_expect_success 'flux-terminus: kill on remote rank' '
	flux terminus kill -r 1  0 &&
	flux terminus list -r 1 >log.list-remote &&
	test_debug "cat log.list-remote"  &&
	grep -q "1 current session" log.list-remote
'
test_expect_success 'flux-terminus: kill-server on remote rank' '
	flux terminus kill-server -r 1 &&
	test_expect_code 1 flux terminus list -r 1
'
test_expect_success 'flux-terminus: start on invalid service name gives error' '
	test_must_fail flux terminus start -s . >log.invalid-service 2>&1 &&
	test_debug "cat log.invalid-service" &&
	grep -q "Invalid argument" log.invalid-service
'
test_expect_success 'flux-terminus: start server on alternate service name' '
	flux terminus start -ds foo &&
	flux terminus list -s foo >log.list-foo &&
	test_debug "cat log.list-foo"  &&
	grep -q "1 current session" log.list-foo
'
test_expect_success 'flux-terminus: start session on alternate service name' '
	flux terminus start -ds foo &&
	flux terminus list -s foo >log.list-foo &&
	test_debug "cat log.list-foo"  &&
	grep -q "2 current sessions" log.list-foo
'
test_expect_success 'flux-terminus: kill on alternate service name' '
	flux terminus kill -s foo  0 &&
	flux terminus list -s foo >log.list-foo &&
	test_debug "cat log.list-foo"  &&
	grep -q "1 current session" log.list-foo
'
test_expect_success 'flux-terminus: kill-server on alternate service name' '
	flux terminus kill-server -s foo &&
	test_expect_code 1 flux terminus list -s foo
'
test_expect_success 'flux-terminus: start can set session name' '
	flux terminus start -d -n test-name &&
	flux terminus list | grep "\[test-name\]"
'
test_expect_success 'flux-terminus: start and set --wait' '
	flux terminus start --wait -d -n waiter true &&
	flux terminus list >log.start-wait 2>&1 &&
	test_debug "cat log.start-wait" &&
	grep -q "\[waiter\]" log.start-wait
'
test_expect_success 'flux-terminus: clean up' '
	flux terminus kill-server
'
# list w/ backoff waiting for server to exit
server_list_thrice() {
	flux terminus list &&
	sleep 0.25 &&
	flux terminus list &&
	sleep 1 &&
	flux terminus list
}
test_expect_success 'flux-terminus: basic start, server exits after session' '
	$runpty flux terminus start sleep 0 &&
	test_expect_code 1 server_list_thrice
'
test_expect_success 'flux-terminus: attach reports exit status' '
	flux terminus start -w -d true &&
	$runpty flux terminus attach 0 &&
	flux terminus start -w -d sh -c "exit 7" &&
	test_expect_code 7 $runpty flux terminus attach 0 &&
	flux terminus start -w -d sh -c "kill -INT \$$" &&
	test_expect_code 130 $runpty flux terminus attach 0
'
# N.B.: We use !wait $pid below because we expect pid to have been
#  stopped by SIGKILL, something neither test_must_fail() nor
#  test_expect_code() handles.
#
test_expect_success NO_CHAIN_LINT 'flux-terminus: start, try a resize' '
	$runpty -o log.resize flux terminus start sleep 1000 &
	pid=$! &&
	$waitfile -t 20 -v -p \"o\" log.resize &&
	test_debug "echo pid=$pid" &&
	kill -WINCH $pid &&
	flux terminus kill 0 &&
	test_debug "cat log.resize" &&
	! wait $pid
'
test_expect_success NO_CHAIN_LINT 'flux-terminus: detach works' '
	$runpty -o log.detach flux terminus start -n test-detach &
	pid=$! &&
	$waitfile -t 20 -v -p \"o\" log.detach &&
	kill -USR1 $pid &&
	$waitfile -t 20 -v -p detached log.detach &&
	wait $pid &&
	flux terminus list >detach.list 2>&1 &&
	test_debug "cat detach.list" &&
	grep "test-detach.*0 clients" detach.list
'
test_expect_success NO_CHAIN_LINT 'flux-terminus: reattach' '
	$runpty -o log.reattach flux terminus attach 0 &
	pid=$! &&
	$waitfile -t 20 -v -p \"o\" log.reattach &&
	flux terminus list >reattach.list 2>&1 &&
	test_debug "cat reattach.list" &&
	grep -q "1 client" reattach.list
	flux terminus kill 0 &&
	! wait $pid
'
test_expect_success NO_CHAIN_LINT 'flux-terminus: copy stdin' '
	$runpty -o log.pipe-stdin flux terminus start &
	pid=$! &&
	$waitfile -t 20 -v -p \"o\" log.pipe-stdin &&
	printf "echo hello\r" | flux terminus attach -p 0 &&
	$waitfile -t 20 -v -p hello log.pipe-stdin &&
	printf "exit\r" | flux terminus attach -p 0 &&
	$waitfile -t 20 -v -p detached log.pipe-stdin &&
	wait $pid
'
test_expect_success NO_CHAIN_LINT 'flux-terminus: disconnect works' '
	flux terminus kill-server
	$runpty -o log.disconnect flux terminus start &
	pid=$! &&
	$waitfile -t 20 -v -p \"o\" log.disconnect &&
	kill -TERM $pid &&
	test_expect_code 143 wait $pid &&
	flux terminus list >disconnect.list &&
	test_debug "cat disconnect.list" &&
	grep -q "0 clients" disconnect.list &&
	flux terminus kill 0
'
test_expect_success 'flux-terminus: nesting not allowed' '
	test_expect_code 1 \
	    $runpty flux terminus start \
	    flux terminus start true
'
test_expect_success 'flux-terminus: requests from invalid userid are rejected' '
	flux terminus start -d &&
	( SERVICE="$(id -u)-terminus" &&
          export FLUX_HANDLE_USERID=$(($(id -u) + 1)) &&
	  export FLUX_HANDLE_ROLEMASK=0x2 &&
	  test_expect_code 1 flux terminus attach -s $SERVICE 0 &&
	  test_expect_code 1 flux terminus list -s $SERVICE &&
	  test_expect_code 1 flux terminus start -s $SERVICE &&
	  test_expect_code 1 flux terminus kill -s $SERVICE 0 &&
	  test_expect_code 1 flux terminus kill-server -s $SERVICE
	)
'
test_done
