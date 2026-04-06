#!/bin/sh

test_description='Test flux cron service'

. $(dirname $0)/sharness.sh
if ! test_have_prereq NO_ASAN; then
    skip_all='skipping faketime tests since AddressSanitizer is active'
    test_done
fi

if test "$(uname -m)" = "aarch64" ; then
    skip_all='skipping faketime cron tests on aarch64'
    test_done
fi

# allow libfaketime to be found on ubuntu, centos
if test -d /usr/lib/x86_64-linux-gnu/faketime ; then
  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/lib/x86_64-linux-gnu/faketime"
elif test -d /usr/lib64/faketime ; then
  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/lib64/faketime"
fi

#  Check for libfaketimeMT using known epoch with /bin/date:
t=$(LD_PRELOAD=libfaketimeMT.so.1 FAKETIME="1973-11-29 21:33:09 UTC" date +%s)
if test "$t" != "123456789" ; then
    skip_all='libfaketime not found. Skipping all tests'
    test_done
fi

# Skip tests if libfaketime < 0.9.10 (known working version)
have_faketime_min_version() {
    python3 -c "
import subprocess, re, sys
try:
    out = subprocess.check_output(['faketime', '--version'],
                                  stderr=subprocess.STDOUT,
                                  text=True)
    m = re.search(r'[0-9]+\.[0-9]+\.[0-9]+', out)
    if not m:
        sys.exit(1)
    ver = tuple(int(x) for x in m.group().split('.'))
    sys.exit(0 if ver >= (0, 9, 10) else 1)
except (FileNotFoundError, subprocess.CalledProcessError):
    sys.exit(1)
" 2>/dev/null
}
if ! have_faketime_min_version; then
    skip_all='libfaketime < v0.9.10. Skipping faketime tests'
    test_done
fi


SIZE=1
export FLUX_TEST_DISABLE_TIMEOUT=t
export LD_PRELOAD=libfaketimeMT.so.1
export FAKETIME_NO_CACHE=1
export FAKETIME_TIMESTAMP_FILE=$(pwd)/faketimerc
# Pre-create faketimerc before the broker starts so libfaketime has a valid
# timestamp from the first gettimeofday() call in the broker.  The initial
# time is set to 1 second before the time used in the first test so the
# broker never experiences a backward time jump.  Without this the broker
# starts in real time (~current year) and then jumps backward to 1991 when
# test 1 runs set_faketime, which can confuse libev.
# Only write this outside the broker context (TEST_UNDER_FLUX_ACTIVE is set
# when the test script is re-invoked inside flux-start).
if test -z "$TEST_UNDER_FLUX_ACTIVE" ; then
    echo '@1991-06-04 00:00:00' > ${FAKETIME_TIMESTAMP_FILE}
fi
test_under_flux ${SIZE} minimal -Slog-stderr-level=1

cron_entry_check() {
    local id=$1
    local key=$2
    local expected="$3"
    test -n $id || return 1
    test -n $key || return 1
    test -n "$expected" || return 1
    local result="$(flux cron dump --key=${key} ${id})" || return 1
    if test "$key" = "typedata.next_wakeup"; then
        # convert result to an integer:
        result=$(printf "%.0f" $result)
    fi
    echo "cron-${id}: ${key}=${result}, wanted ${expected}" >&2
    test "${result}" = "${expected}"
}

flux_cron() {
    result=$(flux cron "$@" 2>&1) || return 1
    id=$(echo ${result} | sed 's/^.*cron-\([0-9][0-9]*\).*/\1/')
    test -n "$id" && echo ${id}
}

# Create a script to manipulate faketime:
cat >make-faketime.sh <<EOF
#!/bin/sh
d="\$@"
# Capture date output first so that a failed date(1) call does not
# silently empty FAKETIME_TIMESTAMP_FILE via the output redirection.
ts=\$(date +"@%Y-%m-%d %H:%M:%S" --date="\${d}") || {
    echo "make-faketime: date failed for '\${d}'" >&2
    exit 1
}
printf '%s\n' "\${ts}" > ${FAKETIME_TIMESTAMP_FILE}.tmp
sync
# Use mv so timestamp file update is atomic
mv ${FAKETIME_TIMESTAMP_FILE}.tmp ${FAKETIME_TIMESTAMP_FILE}
echo timestamp file: \$(cat $FAKETIME_TIMESTAMP_FILE): date is now \$(date) >&2
# Poke flux cron so libev wakes up and reifies time:
flux cron list >/dev/null 2>&1 || :
EOF
chmod +x make-faketime.sh

set_faketime=$(pwd)/make-faketime.sh
event_trace="run_timeout 5 $SHARNESS_TEST_SRCDIR/scripts/event-trace.lua"

within_two() {
    local result=$1
    shift
    local exact=$1
    local two_after=$(($exact + 2))
    test "$result" -ge $exact && test "$result" -le $two_after
}

#  Why does date need to be set 1s in the future??
test_expect_success 'libfaketime works' '
    $set_faketime Jun 4 1991 00:00:01 &&
    flux logger "libfaketime-test" &&
    within_two "$(date +%s)" $(date +%s --date="Jun 4 1991 00:00:00") &&
    date +%s &&
    flux dmesg | grep libfaketime-test
'
test_expect_success 'load cron module' '
    $set_faketime Jun 1 15:00 2016 &&
    flux module load cron
'
test_expect_success 'flux-cron tab works for set minute' '
    $set_faketime Jun 1 15:30 2016 &&
    id=$(echo "15 * * * * flux event pub t.cron.complete" | flux_cron tab) &&
    next=$(date +%s --date="Jun 1 16:15 2016") &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    echo sleeping at $(date) &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime Jun 1 16:15 2016 &&
    echo done at $(date) &&
    cron_entry_check ${id} stats.count 1 &&
    flux cron delete ${id}
'
test_expect_success 'flux-cron tab works for any day midnight' '
    id=$(echo "0 0 * * * flux event pub t.cron.complete" | flux_cron tab) &&
    next=$(date +%s --date="Jun 2 00:00 2016") &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime Jun 2 00:00 2016 &&
    cron_entry_check ${id} stats.count 1 &&
    flux cron delete ${id}
'
test_expect_success 'flux-cron tab works for Mondays, midnight' '
    $set_faketime Jun 4 15:45 2016 &&
    id=$(echo "0 0 * * Mon flux event pub t.cron.complete" | flux_cron tab) &&
    next=$(date +%s --date="Jun 6 00:00 2016") &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime Jun 6 00:00 2016 &&
    cron_entry_check ${id} stats.count 1 &&
    flux cron delete ${id}
'
test_expect_success 'flux-cron tab works for day of month' '
    $set_faketime Jun 5 15:45 2016 &&
    id=$(echo "0 0 30 * * flux event pub t.cron.complete" | flux_cron tab) &&
    next=$(date +%s --date="Jun 30 00:00 2016") &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime Jun 30 00:00 2016 &&
    cron_entry_check ${id} stats.count 1 &&
    flux cron delete ${id}
'
test_expect_success 'flux-cron tab works for month' '
    $set_faketime Jun 5 15:45 2016 &&
    id=$(echo "0 0 30 Dec * flux event pub t.cron.complete" | flux_cron tab) &&
    next=$(date +%s --date="Dec 30 00:00 2016") &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime Dec 30 00:00 2016 &&
    cron_entry_check ${id} stats.count 1 &&
    flux cron delete ${id}
'
test_expect_success 'flux-cron at works' '
    $set_faketime Jun 5 15:45 2016 &&
    id=$(flux_cron at "Jun 6 15:45:00 2016" flux event pub t.cron.complete) &&
    test_when_finished "flux cron delete ${id}" &&
    next=$(date +%s --date="Jun 6 15:45 2016") &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime Jun 6 15:45 2016 &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} stopped true
'
test_expect_success 'relative flux-cron at works' '
    $set_faketime Jun 5 15:45:00 2016 &&
    next=$(date +%s --date="+1 hour") &&
    id=$(flux_cron at "+1 hour" flux event pub t.cron.complete) &&
    test_when_finished "flux cron delete ${id}" &&
    cron_entry_check ${id} type datetime &&
    cron_entry_check ${id} stopped false &&
    cron_entry_check ${id} typedata.next_wakeup ${next} &&
    ${event_trace} t.cron t.cron.complete \
        $set_faketime +1 hour &&
    cron_entry_check ${id} stats.count 1 &&
    cron_entry_check ${id} stopped true
'
test_expect_success 'remove cron module' '
    flux module remove cron
'
test_done
