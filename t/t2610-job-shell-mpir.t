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
    flux job wait-event -f json -p guest.exec.eventlog $1 shell.init | \
        jq '.context["leader-rank"]'
}
shell_service() {
    flux job wait-event -f json -p guest.exec.eventlog $1 shell.init | \
        jq -r '.context["service"]'
}

for test in 1:1 2:2 2:4 4:4 4:8 4:7; do
    TASKS=${test#*:}
    NODES=${test%:*}
    test_expect_success "flux-shell: ${NODES}N/${TASKS}P: trace+mpir works" '
	id=$(flux submit -o stop-tasks-in-exec \
             -n${TASKS} -N${NODES} true)  &&
        flux job wait-event -vt 5 -p guest.exec.eventlog \
                -m sync=true ${id} shell.start &&
        ${mpir} $(shell_leader_rank $id) $(shell_service $id) &&
        flux job kill -s CONT ${id} &&
        flux job attach ${id}
    '
done


test_expect_success 'flux-shell: test security of proctable method' '
    id=$(flux submit -o stop-tasks-in-exec true) &&
    flux job wait-event -vt 5 -p guest.exec.eventlog \
        -m sync=true ${id} shell.start &&
    shell_rank=$(shell_leader_rank $id) &&
    shell_service=$(shell_service $id) &&
    ( export FLUX_HANDLE_USERID=9999 &&
      export FLUX_HANDLE_ROLEMASK=0x2 &&
      test_expect_code 1 ${mpir} $shell_rank $shell_service
    ) &&
    ${mpir} $(shell_leader_rank $id) $(shell_service $id) &&
    flux job kill -s CONT ${id} &&
    flux job attach ${id}
'

test_done
