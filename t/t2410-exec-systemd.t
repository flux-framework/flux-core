#!/bin/sh
#

test_description='Test simulated job exection with system instance'

. `dirname $0`/sharness.sh

if ! flux version | grep systemd; then
    skip_all='flux not built with systemd'
    test_done
fi

#  Don't run if systemd environment variables not setup
if test -z "$DBUS_SESSION_BUS_ADDRESS" \
     -o -z "$XDG_RUNTIME_DIR"; then
    skip_all='DBUS_SESSION_BUS_ADDRESS and/or XDG_RUNTIME_DIR not set'
    test_done
fi

uid=`id -u`
userservice="user@${uid}.service"
if ! systemctl list-units | grep ${userservice}
then
    skip_all='systemd user service not running'
    test_done
fi

test_under_flux 1 full

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

test_expect_success 'run process under systemd and wait till exits (success)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run-wait-exit --unitname=simtest-success \
            /bin/true >& run-wait-exitA.out &&
        grep simtest-success run-wait-exitA.out | grep exited | grep "status=0"
'

test_expect_success 'run process under systemd and wait till exits (failure)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run-wait-exit --unitname=simtest-failure \
            /bin/false >& run-wait-exitB.out &&
        grep simtest-failure run-wait-exitB.out | grep exited | grep "status=1"
'

# N.B. SIGTERM=15
test_expect_success NO_CHAIN_LINT 'run process under systemd and wait till exits (signal)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run-wait-exit --unitname=simtest-signal \
            /usr/bin/sleep 30 >& run-wait-exitC.out &
        $waitfile --count=1 --timeout=10 --pattern=active run-wait-exitC.out &&
        systemctl kill --user simtest-signal.service &&
        $waitfile --count=1 --timeout=10 --pattern=exited run-wait-exitC.out &&
        grep simtest-signal run-wait-exitC.out | grep exited | grep "status=15"
'

test_expect_success 'run process under systemd and let it keep running (success)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run --unitname=simtest-reattach-run-success \
            ${SHARNESS_TEST_DIRECTORY}/sdprocess/job-signal-exit 60 >& runD.out &&
        grep simtest-reattach-run-success runD.out | grep active &&
        systemctl list-units --user | grep simtest-reattach-run-success | grep active
'
# achu: one might wonder why SIGINT/SIGTERM is used for tests below instead of something
# generic like SIGUSR1.
#
# 1) systemctl only supports a few special signals like SIGINT, SIGTERM
#
# 2) systemd knows to treat SIGINT, SIGTERM special and preserve the
# exit status from the process

# SIGINT will make job-signal-exit with status 0
test_expect_success NO_CHAIN_LINT 'attach to running process and wait for exit (success)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            wait --unitname=simtest-reattach-run-success >& waitE.out &
        $waitfile --count=1 --timeout=10 --pattern=attached waitE.out &&
        $waitfile --count=1 --timeout=10 --pattern=active waitE.out &&
        systemctl kill --user --signal=SIGINT simtest-reattach-run-success.service &&
        $waitfile --count=1 --timeout=10 --pattern=exited waitE.out &&
        grep simtest-reattach-run-success waitE.out | grep exited | grep status=0
'

test_expect_success 'run process under systemd and let it keep running (failure)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run --unitname=simtest-reattach-run-failure \
            ${SHARNESS_TEST_DIRECTORY}/sdprocess/job-signal-exit 60 >& runF.out &&
        grep simtest-reattach-run-failure runF.out | grep active &&
        systemctl list-units --user | grep simtest-reattach-run-failure | grep active
'

# SIGTERM will make job-signal-exit with status 1
test_expect_success NO_CHAIN_LINT 'attach to running process and wait for exit (failure)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            wait --unitname=simtest-reattach-run-failure >& waitG.out &
        $waitfile --count=1 --timeout=10 --pattern=attached waitG.out &&
        $waitfile --count=1 --timeout=10 --pattern=active waitG.out &&
        systemctl kill --user --signal=SIGTERM simtest-reattach-run-failure.service &&
        $waitfile --count=1 --timeout=10 --pattern=exited waitG.out &&
        grep simtest-reattach-run-failure waitG.out | grep exited | grep status=1
'

test_expect_success 'run process under systemd and let it keep running (failure)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run --unitname=simtest-reattach-run-failure \
            /usr/bin/sleep 60 >& runH.out &&
        grep simtest-reattach-run-failure runH.out | grep active &&
        systemctl list-units --user | grep simtest-reattach-run-failure | grep active
'

# N.B. SIGTERM=15
test_expect_success NO_CHAIN_LINT 'attach to running process and wait for exit (failure)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            wait --unitname=simtest-reattach-run-failure >& waitI.out &
        $waitfile --count=1 --timeout=10 --pattern=attached waitI.out &&
        $waitfile --count=1 --timeout=10 --pattern=active waitI.out &&
        systemctl kill --user --signal=SIGTERM simtest-reattach-run-failure.service &&
        $waitfile --count=1 --timeout=10 --pattern=exited waitI.out &&
        grep simtest-reattach-run-failure waitI.out | grep exited | grep status=15
'

test_expect_success 'attach to process that finished (success)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run-wait-exit --unitname=simtest-reattach-exit-success --no-cleanup \
            /bin/true >& run-wait-exitJ.out &&
        grep simtest-reattach-exit-success run-wait-exitJ.out \
            | grep exited | grep "status=0"
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            wait --unitname=simtest-reattach-exit-success >& waitJ.out &&
        grep simtest-reattach-exit-success waitJ.out | grep exited | grep status=0
'

test_expect_success 'attach to process that finished (failure)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run-wait-exit --unitname=simtest-reattach-exit-failure --no-cleanup \
            /bin/false >& run-wait-exitK.out &&
        grep simtest-reattach-exit-failure run-wait-exitK.out \
            | grep exited | grep "status=0"
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            wait --unitname=simtest-reattach-exit-failure >& waitK.out &&
        grep simtest-reattach-exit-failure waitK.out | grep exited | grep status=1
'

# N.B. SIGTERM=15
test_expect_success NO_CHAIN_LINT 'attach to process that finished (signal)' '
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            run-wait-exit --unitname=simtest-reattach-exit-signal --no-cleanup \
            /usr/bin/sleep 30 >& run-wait-exitL.out &
        $waitfile --count=1 --timeout=10 --pattern=active run-wait-exitL.out &&
        systemctl kill --user simtest-reattach-exit-signal.service &&
        $waitfile --count=1 --timeout=10 --pattern=exited run-wait-exitL.out &&
        ${SHARNESS_TEST_DIRECTORY}/sdprocess/sim-broker \
            wait --unitname=simtest-reattach-exit-signal >& waitL.out &&
        grep simtest-reattach-exit-signal waitL.out | grep exited | grep status=15
'

test_done
