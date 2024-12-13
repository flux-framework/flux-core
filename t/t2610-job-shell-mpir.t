#!/bin/sh
#
test_description='Test flux-shell MPIR and ptrace support'

. `dirname $0`/sharness.sh

test_under_flux 4 job

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"

INITRC_TESTDIR="${SHARNESS_TEST_SRCDIR}/shell/initrc"
INITRC_PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"
mpir="${SHARNESS_TEST_DIRECTORY}/shell/mpir"

shell_leader_rank() {
	flux job wait-event -f json -p exec $1 shell.init | \
	    jq '.context["leader-rank"]'
}
shell_service() {
	flux job wait-event -f json -p exec $1 shell.init | \
	    jq -r '.context["service"]'
}

for test in 1:1 2:2 2:4 4:4 4:8 4:7; do
    TASKS=${test#*:}
    NODES=${test%:*}
    test_expect_success "flux-shell: ${NODES}N/${TASKS}P: trace+mpir works" '
	id=$(flux submit -o stop-tasks-in-exec \
	    -n${TASKS} -N${NODES} true)  &&
	flux job wait-event -vt 5 -p exec -m sync=true ${id} shell.start &&
	${mpir} -r $(shell_leader_rank $id) -s $(shell_service $id) &&
	flux job kill -s CONT ${id} &&
	flux job attach ${id}
    '
done


test_expect_success 'flux-shell: test security of proctable method' '
	id=$(flux submit -o stop-tasks-in-exec true) &&
	flux job wait-event -vt 5 -p exec -m sync=true ${id} shell.start &&
	shell_rank=$(shell_leader_rank $id) &&
	shell_service=$(shell_service $id) &&
	( export FLUX_HANDLE_USERID=9999 &&
	  export FLUX_HANDLE_ROLEMASK=0x2 &&
	  test_expect_code 1 ${mpir} -r $shell_rank -s $shell_service
	) &&
	${mpir} -r $(shell_leader_rank $id) -s $(shell_service $id) &&
	flux job kill -s CONT ${id} &&
	flux job attach ${id}
'
test_expect_success 'mpir: tool launch is supported' '
	id=$(flux submit -N4 -n8 -o stop-tasks-in-exec true) &&
	flux job wait-event -vt 5 -p exec -m sync=true $id shell.start &&
	shell_rank=$(shell_leader_rank $id) &&
	shell_service=$(shell_service $id) &&
	${mpir} -r ${shell_rank} -s ${shell_service} \
	    --tool-launch /bin/echo this is a tool >tool.log 2>&1 &&
	test_debug "cat tool.log" &&
	grep "MPIR: rank 0: echo: stdout: this is a tool" tool.log &&
	grep "MPIR: rank 1: echo: stdout: this is a tool" tool.log &&
	grep "MPIR: rank 2: echo: stdout: this is a tool" tool.log &&
	grep "MPIR: rank 3: echo: stdout: this is a tool" tool.log &&
	test $(grep -c "MPIR:.*this is a tool" tool.log) -eq 4 &&
	flux job kill -s CONT $id &&
	flux job attach $id
'
# Empty args for MPIR_executable_path -- also run with shell leader not
# on broker rank 0 to ensure this configuration works with tool launch
test_expect_success 'mpir: tool launch works with empty MPIR_server_arguments' '
	id=$(flux submit --requires=rank:1,3 \
	     -N2 -n2 -o stop-tasks-in-exec true) &&
	flux job wait-event -vt 5 -p exec -m sync=true $id shell.start &&
	shell_rank=$(shell_leader_rank $id) &&
	shell_service=$(shell_service $id) &&
	${mpir} -r ${shell_rank} -s ${shell_service} \
	    --tool-launch hostname >tool2.log 2>&1 &&
	test_debug "cat tool2.log" &&
	grep "MPIR: rank 1: hostname: stdout:" tool2.log &&
	grep "MPIR: rank 3: hostname: stdout:" tool2.log &&
	test $(grep -c "MPIR:.*hostname:" tool2.log) -eq 2 &&
	flux job kill -s CONT $id &&
	flux job attach $id
'
test_expect_success 'mpir: tool launch reports errors' '
	id=$(flux submit -N2 -n2 -o stop-tasks-in-exec true) &&
	flux job wait-event -vt 5 -p exec -m sync=true $id shell.start &&
	shell_rank=$(shell_leader_rank $id) &&
	shell_service=$(shell_service $id) &&
	${mpir} -r ${shell_rank} -s ${shell_service} \
	    --tool-launch nosuchcommand >tool3.log 2>&1 &&
	test_debug "cat tool3.log" &&
	grep "MPIR: rank 0: nosuchcommand: error launching process" tool3.log &&
	flux job kill -s CONT $id &&
	flux job attach $id
'

test_done
