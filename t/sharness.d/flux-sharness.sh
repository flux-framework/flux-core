
#
#  project-local sharness code for Flux
#

# add scripts directory to path
export PATH="${SHARNESS_TEST_SRCDIR}/scripts:$PATH"

#
#  Extra functions for Flux testsuite
#
run_timeout() {
    if test -z "$LD_PRELOAD" ; then
        "${PYTHON:-python3}" "${SHARNESS_TEST_SRCDIR}/scripts/run_timeout.py" "$@"
    else
        (
            TIMEOUT_PRELOAD="$LD_PRELOAD"
            unset -v LD_PRELOAD
            exec "${PYTHON:-python3}" -S "${SHARNESS_TEST_SRCDIR}/scripts/run_timeout.py" -e LD_PRELOAD="$TIMEOUT_PRELOAD" "$@"
        )
    fi
}

#
#  Echo on stdout a reasonable size for a large test session,
#   controllable test-wide via env vars FLUX_TEST_SIZE_MIN and
#   FLUX_TEST_SIZE_MAX.
#
test_size_large() {
    min=${FLUX_TEST_SIZE_MIN:-4}
    max=${FLUX_TEST_SIZE_MAX:-17}
    size=$(($(nproc)+1))
    test ${size} -lt ${min} && size=$min
    test ${size} -gt ${max} && size=$max
    echo ${size}
}


#
#  Like test_must_fail(), but additionally allow process to be
#   terminated by SIGKILL or SIGTERM
#
test_must_fail_or_be_terminated() {
    "$@"
    exit_code=$?
    # Allow death by SIGTERM or SIGKILL
    if test $exit_code = 143 -o $exit_code = 137; then
        return 0
    elif test $exit_code = 0; then
        echo >&2 "test_must_fail: command succeeded: $*"
        return 1
    elif test $exit_code -gt 129 -a $exit_code -le 192; then
        echo >&2 "test_must_fail: died by non-SIGTERM signal: $*"
        return 1
    elif test $exit_code = 127; then
        echo >&2 "test_must_fail: command not found: $*"
        return 1
    fi
    return 0
}

#
#  Tests using test_under_flux() and which load their own modules should
#   ensure those modules are unloaded at the end of the test for proper
#   cleanup and test coverage (also as a general principle, module unload
#   should be something explicitly tested).
#
#  The functions below ensure that every module loaded at "test_done" time
#   when test_under_flux() was used was also loaded before test_under_flux
#   was called.
#
flux_module_list() {
    flux module list | awk '!/^Module/{print $1}' | sort
}

check_module_list() {
    flux_module_list > module-list.final
    while read module; do
       grep "^$module$" module-list.initial >/dev/null 2>&1 \
            || bad="${bad}${bad:+ }$module"
    done < module-list.final
    if test -n "$bad"; then
        test -n "$logfile" \
            && say_color error >&3 \
                 "Error: manually loaded module(s) not unloaded: $bad"
        # This function is run under test_eval_ so redirect
        #  error message to &5 (saved stdout) so text doesn't disappear:
        error >&5 2>&1 "manually loaded module(s) not unloaded: $bad"
    fi
}

#
#  Generate configuration for test bootstrap and print args for flux-start
#  Usage:  args=$(make_bootstrap_config workdir sockdir size)
#
make_bootstrap_config() {
    local workdir=$1
    local sockdir=$2
    local size=$3
    local fakehosts="fake[0-$(($size-1))]"
    local full="0-$(($size-1))"

    mkdir $workdir/conf.d
    mkdir $workdir/state
    flux keygen --name testcert $workdir/cert
    cat >$workdir/conf.d/bootstrap.toml <<-EOT
	[bootstrap]
	    curve_cert = "$workdir/cert"
	    default_bind = "ipc://$sockdir/tbon-%h"
	    default_connect = "ipc://$sockdir/tbon-%h"
	    hosts = [
	        { host = "$fakehosts" },
	    ]
	EOT
    flux R encode --hosts=$fakehosts -r$full >$workdir/R
    cat >$workdir/conf.d/resource.toml <<-EOT2
	[resource]
	    path = "$workdir/R"
	    noverify = true
	EOT2
    echo "--test-hosts=$fakehosts -o,-c$workdir/conf.d"
    echo "--test-exit-mode=${TEST_UNDER_FLUX_EXIT_MODE:-leader}"
    echo "--test-exit-timeout=${TEST_UNDER_FLUX_EXIT_TIMEOUT:-0}"
    echo "-o,-Sbroker.quorum=${TEST_UNDER_FLUX_QUORUM:-$size}"
    echo "--test-start-mode=${TEST_UNDER_FLUX_START_MODE:-all}"
    echo "-o,-Stbon.topo=${TEST_UNDER_FLUX_TOPO:-custom}"
    echo "-o,-Stbon.zmqdebug=1"
    echo "-o,-Sstatedir=$workdir/state"
}

#
#  Remove any outer trash-directory wrapper used by "system"
#   personality test_under_flux() tests.
#
remove_trashdir_wrapper() {
    local trashdir=$(dirname $SHARNESS_TRASH_DIRECTORY)
    case $trashdir in
        */trash-directory.[!/]*) rm -rf $trashdir
    esac
}

#
#  Reinvoke a test file under a flux instance
#
#  Usage: test_under_flux <size> [personality] [flux-start-options]
#
#  where personality is one of:
#
#  full (default)
#    Run with all services.
#    The default broker rc scripts are executed.
#
#  minimal
#    Run with only built-in services.
#    No broker rc scripts are executed.
#
#  job
#    Load minimum services needed to run jobs.
#    Fake resources are loaded into the resource module.
#    Environment variables:
#    - TEST_UNDER_FLUX_CORES_PER_RANK
#        Set the number of fake cores per fake node (default: 2).
#    - TEST_UNDER_FLUX_NO_JOB_EXEC
#        If set, skip loading job-exec module (default: load job-exec).
#    - TEST_UNDER_FLUX_SCHED_SIMPLE_MODE
#        Change mode argument to sched-simple (default: limited=8)
#
#  kvs
#    Load minimum services needed for kvs.
#
#  system
#    Like full, but bootstrap with a generated config file.
#    Environment variables:
#    - TEST_UNDER_FLUX_EXIT_MODE
#        Set the flux-start exit mode (default: leader)
#    - TEST_UNDER_FLUX_EXIT_TIMEOUT
#        Set the flux-start exit timeout (default: 0)
#    - TEST_UNDER_FLUX_QUORUM
#        Set the broker.quorum attribute (default: <size>)
#    - TEST_UNDER_FLUX_START_MODE
#        Set the flux-start start mode (default: all)
#    - TEST_UNDER_FLUX_TOPO
#        Set the TBON topology (default: custom (flat))
#
test_under_flux() {
    size=${1:-1}
    personality=${2:-full}

    #  Note: args > 2 are passed along as extra arguments
    #   to flux-start below using "$@", so shift up to the
    #   the first two arguments away:
    #
    test $# -eq 1 && shift || shift 2

    log_file="$TEST_NAME.broker.log"
    if test -n "$TEST_UNDER_FLUX_ACTIVE" ; then
        if test "$TEST_UNDER_FLUX_PERSONALITY" = "system"; then
            test "$debug" = "t" || cleanup remove_trashdir_wrapper
        fi
        test "$debug" = "t" || cleanup rm "${SHARNESS_TEST_DIRECTORY:-..}/$log_file"
        flux_module_list > module-list.initial
        cleanup check_module_list
        return
    fi
    if test "$verbose" = "t" -o -n "$FLUX_TESTS_DEBUG" ; then
        flags="${flags} --verbose"
    fi
    if test "$debug" = "t" -o -n "$FLUX_TESTS_DEBUG" ; then
        flags="${flags} --debug"
        export FLUX_PYCLI_LOGLEVEL=10
    fi
    if test "$chain_lint" = "t"; then
        flags="${flags} --chain-lint"
    fi
    if test -n "$logfile" -o -n "$FLUX_TESTS_LOGFILE" ; then
        flags="${flags} --logfile"
    fi
    if test -n "$SHARNESS_TEST_DIRECTORY"; then
        cd $SHARNESS_TEST_DIRECTORY
    fi

    if test "$personality" = "minimal"; then
        RC1_PATH=""
        RC3_PATH=""
    elif test "$personality" = "system"; then
        # Pre-create broker rundir so we know it in advance and
        # make_bootstrap_config() can use it for ipc:// socket paths.
        BROKER_RUNDIR=$(mktemp --directory --tmpdir flux-system-XXXXXX)
        sysopts=$(make_bootstrap_config \
          $SHARNESS_TRASH_DIRECTORY $BROKER_RUNDIR $size)
        # Place the re-executed test script trash within the first invocation's
        # trash to preserve config files for broker restart in test
        flags="${flags} --root=$SHARNESS_TRASH_DIRECTORY"
        unset root
    elif test "$personality" != "full"; then
        RC1_PATH=$FLUX_SOURCE_DIR/t/rc/rc1-$personality
        RC3_PATH=$FLUX_SOURCE_DIR/t/rc/rc3-$personality
        test -x $RC1_PATH || error "cannot execute $RC1_PATH"
        test -x $RC3_PATH || error "cannot execute $RC3_PATH"
    else
        unset RC1_PATH
        unset RC3_PATH
    fi

    if test -n "$root"; then
        flags="${flags} --root=$root"
    fi

    if test -n "$FLUX_TEST_VALGRIND" ; then
        VALGRIND_SUPPRESSIONS=${SHARNESS_TEST_SRCDIR}/valgrind/valgrind.supp
        valgrind="--wrap=libtool,e"
        valgrind="$valgrind,valgrind,--leak-check=full"
        valgrind="$valgrind,--trace-children=no,--child-silent-after-fork=yes"
        valgrind="$valgrind,--leak-resolution=med,--error-exitcode=1"
        valgrind="$valgrind,--suppressions=${VALGRIND_SUPPRESSIONS}"
    elif test -n "$FLUX_TEST_HEAPTRACK" ; then
        valgrind="--wrap=heaptrack,--record-only"
    elif test -n "$FLUX_TEST_WRAP" ; then
        valgrind="$FLUX_TEST_WRAP"
    fi
    # Extend timeouts when running under AddressSanitizer
    if test_have_prereq ASAN; then
        # Set log_path for ASan o/w errors from broker may be lost
        ASAN_OPTIONS=${ASAN_OPTIONS}:log_path=${TEST_NAME}.asan
    fi
    logopts="-o -Slog-filename=${log_file},-Slog-forward-level=7"
    TEST_UNDER_FLUX_ACTIVE=t \
    TERM=${ORIGINAL_TERM} \
    TEST_UNDER_FLUX_PERSONALITY="${personality:-default}" \
      exec flux start --test-size=${size} \
                      ${BROKER_RUNDIR+--test-rundir=${BROKER_RUNDIR}} \
                      ${BROKER_RUNDIR+--test-rundir-cleanup} \
                      ${RC1_PATH+-o -Sbroker.rc1_path=${RC1_PATH}} \
                      ${RC3_PATH+-o -Sbroker.rc3_path=${RC3_PATH}} \
                      ${sysopts} \
                      ${logopts} \
                      ${valgrind} \
                      "$@" \
                     "sh $0 ${flags}"
}

mock_bootstrap_instance() {
    if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
        unset FLUX_URI
    fi
}

#
#  Execute arguments $2-N on rank or ranks specified in arg $1
#   using the flux-exec utility
#
test_on_rank() {
    test "$#" -ge 2 ||
        error "test_on_rank expects at least two parameters"
    test -n "$TEST_UNDER_FLUX_ACTIVE"  ||
        error "test_on_rank: test_under_flux not active ($TEST_UNDER_FLUX_ACTIVE)"

    ranks=$1; shift;
    flux exec --rank=${ranks} "$@"
}

#  Note: Some versions of bash may cause the `flux` libtool wrapper script
#  to reset the COLUMNS shell variable even if it is explicitly set for
#  for testing purposes. This causes tests that check for output truncation
#  based on COLUMNS to erroneously fail. (Note: this only seems to be the
#  case when tests are run with --debug --verbose for unknown reasons.
#
#  Add a script for tests that use COLUMNS to check if the variable will
#  be preserved across an invovation of flux(1) so they may set a prereq
#  and skip tests that might erroneous fail if COLUMNS is not preserved.
#
test_columns_variable_preserved() {
	local cols=$(COLUMNS=12 \
	             flux python -c \
	             "import shutil; print(shutil.get_terminal_size().columns)")
	test "$cols" = "12"
}

#  Export a shorter name for this test
TEST_NAME=$SHARNESS_TEST_NAME
export TEST_NAME

#  Test requirements for testsuite
if ! command -v jq >/dev/null; then
    error "jq is required for the flux-core testsuite"
fi
if ! run_timeout 10.0 lua -e 'require "posix"'; then
    error "failed to find lua posix module in path"
fi
jq=$(command -v jq)

#  Some tests in flux don't work with --chain-lint, add a prereq for
#   --no-chain-lint:
test "$chain_lint" = "t" || test_set_prereq NO_CHAIN_LINT

#  Set LONGTEST prereq
if test "$TEST_LONG" = "t" || test "$LONGTEST" = "t"; then
    test_set_prereq LONGTEST
fi

#  Set ASAN or NO_ASAN prereq
if flux version | grep -q +asan; then
    test_set_prereq ASAN
else
    test_set_prereq NO_ASAN
fi

# Sanitize PMI_* environment for all tests. This allows commands like
#  `flux broker` in tests to boot as singleton even when run under a
#  job of an existing RM.
for var in $(env | grep ^PMI); do unset ${var%%=*}; done
for var in $(env | grep ^SLURM); do unset ${var%%=*}; done

# Sanitize Flux environment variables that should not be inherited by
#  tests
unset FLUX_SHELL_RC_PATH
unset FLUX_RC_EXTRA
unset FLUX_CONF_DIR
unset FLUX_JOB_CC
unset FLUX_F58_FORCE_ASCII

# Individual tests that need to force local URI resolution should set
#  this specifically. In general it breaks other URI tests:
unset FLUX_URI_RESOLVE_LOCAL

# Set XDG_CONFIG_DIRS and XDG_CONFIG_HOME to a nonexistent directory to
#  avoid system or user configuration influencing tests for utilities
#  that use flux.util.UtilConfig or other config classes utilizing
#  the XDG base directory specification.
export XDG_CONFIG_DIRS=/noexist
export XDG_CONFIG_HOME=/noexist

# vi: ts=4 sw=4 expandtab
