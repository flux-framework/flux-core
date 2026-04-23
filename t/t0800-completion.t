#!/bin/bash
#

test_description='Test bash completions for flux commands'

# test_under_flux re-executes this script via 'sh $0'.  Re-exec as bash
# immediately so the rest of the script can use bash-specific features.
[ -z "$BASH_VERSION" ] && exec bash "$0" "$@"

. "$(dirname $0)"/sharness.sh

FLUX_COMPLETION=${SHARNESS_TEST_SRCDIR}/../etc/completions/flux.pre

if ! test -f "$FLUX_COMPLETION"; then
    skip_all='skipping bash completion tests: flux.pre not found'
    test_done
fi

SIZE=2
test_under_flux ${SIZE} job

shopt -s extglob

# compopt is only valid inside a real readline completion context.
# Stub it out so completion functions don't error when called directly.
compopt() { :; }

#
# Stub out 'flux' so static completion tests don't need a live instance.
# This is overridden in the dynamic section below via 'unset -f flux'.
#
flux() {
    local subcmd="$1"; shift
    case "$subcmd" in
    jobs)
        echo "ƒ111111111"
        echo "ƒ222222222"
        ;;
    queue)
        # 'flux queue status' output format: "NAME: ..."
        echo "default: enabled"
        echo "batch: enabled"
        ;;
    env)
        # __get_flux_subcommands calls 'flux env printenv FLUX_EXEC_PATH'
        echo ""
        ;;
    *)
        return 1
        ;;
    esac
}
export -f flux

# shellcheck source=/dev/null
. "$FLUX_COMPLETION"

# Save the real __get_flux_subcommands before the static stub overrides it
_orig_get_flux_subcommands=$(declare -f __get_flux_subcommands)

# Override __get_flux_subcommands so top-level completion works without
# a real FLUX_EXEC_PATH pointing to installed binaries.
__get_flux_subcommands() {
    echo "jobs submit run alloc batch bulksubmit cancel job resource queue \
          overlay config module jobtap pgrep pkill pstree watch top dmesg \
          ping exec kvs cron start broker shutdown archive housekeeping uri \
          modprobe sproc proxy update hostlist dump restore R keygen logger \
          post-job-event fsck startlog pmi uptime version env cgroup \
          heaptrace lptest relay python python3.13 getattr setattr lsattr \
          content admin help"
}

# Temp file used to capture COMPREPLY across function calls
COMP_OUT=$(mktemp)
test_at_end_hook_() { rm -f "$COMP_OUT"; }

#
# run_completion CMDLINE
#
# Sets COMP_WORDS/COMP_CWORD/cur/prev from CMDLINE (last word is $cur),
# calls _flux_core, writes one completion per line to $COMP_OUT.
#
run_completion() {
    local cmdline="$1"
    read -ra COMP_WORDS <<< "$cmdline"
    # If cmdline ends with a space, the current word is empty
    if [[ "$cmdline" == *" " ]]; then
        COMP_WORDS+=("")
    fi
    COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
    cur="${COMP_WORDS[$COMP_CWORD]}"
    prev="${COMP_WORDS[$COMP_CWORD-1]}"
    COMPREPLY=()
    _flux_core
    printf '%s\n' "${COMPREPLY[@]}" > "$COMP_OUT"
}

# completion_has WORD  -- succeeds if WORD appears in last completion result
completion_has() { grep -qxF -- "$1" "$COMP_OUT"; }

# completion_lacks WORD -- succeeds if WORD does NOT appear
completion_lacks() { ! grep -qxF -- "$1" "$COMP_OUT"; }

# completion_has_match PATTERN -- succeeds if any line matches ERE pattern
completion_has_match() { grep -qE -- "$1" "$COMP_OUT"; }

# completion_empty -- succeeds if COMPREPLY was empty (e.g. file completion
# was delegated to readline via compopt -o default).
# Note: COMPREPLY=( $(compgen ...) ) with no matches still produces a
# one-element empty-string array, so we check for any non-empty line content
# rather than file size.
completion_empty() { ! grep -q . "$COMP_OUT"; }

#
# Top-level command completion
#
test_expect_success 'top-level: completes known subcommands' '
    run_completion "flux " &&
    completion_has "jobs" &&
    completion_has "submit" &&
    completion_has "run" &&
    completion_has "cancel"
'
test_expect_success 'top-level: completes flux options' '
    run_completion "flux -" &&
    completion_has "--verbose" &&
    completion_has "--version"
'

#
# flux-pkill
#
test_expect_success 'pkill: completes --wait' '
    run_completion "flux pkill --" &&
    completion_has "--wait"
'
test_expect_success 'pkill: completes --queue' '
    run_completion "flux pkill --" &&
    completion_has "--queue="
'

#
# flux-watch
#
test_expect_success 'watch: completes -v short form' '
    run_completion "flux watch -" &&
    completion_has "-v"
'
test_expect_success 'watch: completes --verbose' '
    run_completion "flux watch --" &&
    completion_has "--verbose"
'

#
# flux-exec
#
test_expect_success 'exec: completes --with-imp' '
    run_completion "flux exec --" &&
    completion_has "--with-imp"
'
test_expect_success 'exec: completes --jobid' '
    run_completion "flux exec --" &&
    completion_has "--jobid="
'
test_expect_success 'exec: --jobid completes with active jobids' '
    run_completion "flux exec --jobid " &&
    completion_has "ƒ111111111"
'

#
# flux-queue
#
test_expect_success 'queue enable: completes --quiet and --verbose' '
    run_completion "flux queue enable --" &&
    completion_has "--quiet" &&
    completion_has "--verbose"
'
test_expect_success 'queue disable: completes --message' '
    run_completion "flux queue disable --" &&
    completion_has "--message="
'
test_expect_success 'queue start: completes --all' '
    run_completion "flux queue start --" &&
    completion_has "--all"
'
test_expect_success 'queue stop: completes --all and --message' '
    run_completion "flux queue stop --" &&
    completion_has "--all" &&
    completion_has "--message="
'
test_expect_success 'queue idle: completes --timeout and --quiet' '
    run_completion "flux queue idle --" &&
    completion_has "--timeout=" &&
    completion_has "--quiet"
'

#
# flux-submit/run/alloc/batch
#
test_expect_success 'submit: completes --quiet' '
    run_completion "flux submit --" &&
    completion_has "--quiet"
'
test_expect_success 'run: completes --quiet' '
    run_completion "flux run --" &&
    completion_has "--quiet"
'
test_expect_success 'batch: completes --broker-opts' '
    run_completion "flux batch --" &&
    completion_has "--broker-opts="
'
test_expect_success 'alloc: completes --broker-opts' '
    run_completion "flux alloc --" &&
    completion_has "--broker-opts="
'

#
# flux-R encode
#
test_expect_success 'R encode: completes --property' '
    run_completion "flux R encode --" &&
    completion_has "--property="
'

#
# flux-dump / flux-restore: file completion for positional arg
#
test_expect_success 'dump: completes options' '
    run_completion "flux dump --" &&
    completion_has "--verbose" &&
    completion_has "--quiet"
'
test_expect_success 'dump: non-option arg delegates to file completion' '
    run_completion "flux dump somefile" &&
    completion_empty
'
test_expect_success 'restore: completes options' '
    run_completion "flux restore --" &&
    completion_has "--verbose" &&
    completion_has "--quiet"
'
test_expect_success 'restore: non-option arg delegates to file completion' '
    run_completion "flux restore somefile" &&
    completion_empty
'

#
# flux-fsck
#
test_expect_success 'fsck: completes --verbose and --quiet' '
    run_completion "flux fsck --" &&
    completion_has "--verbose" &&
    completion_has "--quiet"
'
test_expect_success 'fsck: completes --repair and --job-aware' '
    run_completion "flux fsck --" &&
    completion_has "--repair" &&
    completion_has "--job-aware"
'
test_expect_success 'fsck: --rootref takes an argument (no completion)' '
    run_completion "flux fsck --rootref " &&
    completion_empty
'

#
# flux-startlog
#
test_expect_success 'startlog: completes --check and --quiet' '
    run_completion "flux startlog --" &&
    completion_has "--check" &&
    completion_has "--quiet"
'
test_expect_success 'startlog: completes --show-version' '
    run_completion "flux startlog --" &&
    completion_has "--show-version"
'

#
# flux-pmi
#
test_expect_success 'pmi: completes subcommands' '
    run_completion "flux pmi " &&
    completion_has "barrier" &&
    completion_has "get" &&
    completion_has "exchange"
'
test_expect_success 'pmi: completes --method and --verbose' '
    run_completion "flux pmi --" &&
    completion_has "--method=" &&
    completion_has "--verbose="
'
test_expect_success 'pmi barrier: completes --test-count and --test-timing' '
    run_completion "flux pmi barrier --" &&
    completion_has "--test-count=" &&
    completion_has "--test-timing"
'
test_expect_success 'pmi get: completes --ranks' '
    run_completion "flux pmi get --" &&
    completion_has "--ranks="
'
test_expect_success 'pmi exchange: completes --count' '
    run_completion "flux pmi exchange --" &&
    completion_has "--count="
'

#
# flux-pythonX.Y / flux-python-version: versioned python wrappers
#
test_expect_success 'python3: prefix completes to versioned pythonX.Y command' '
    run_completion "flux python3" &&
    completion_has_match "^python[0-9]+\.[0-9]" &&
    completion_lacks "python3"
'
test_expect_success 'python3.13: completes --get-path' '
    run_completion "flux python3.13 --" &&
    completion_has "--get-path"
'
test_expect_success 'python3.13: non-option arg delegates to file completion' '
    run_completion "flux python3.13 script.py" &&
    completion_empty
'
test_expect_success 'python: does not complete --get-path' '
    run_completion "flux python --" &&
    completion_lacks "--get-path"
'

#
# flux-batch: file completion for script arg
#
test_expect_success 'batch: non-option arg delegates to file completion' '
    run_completion "flux batch myscript" &&
    completion_empty
'

#
# flux-module load / flux-jobtap load: file completion for plugin path
#
test_expect_success 'module load: non-option arg delegates to file completion' '
    run_completion "flux module load /path/to/mod" &&
    completion_empty
'
test_expect_success 'jobtap load: non-option arg delegates to file completion' '
    run_completion "flux jobtap load /path/to/plugin" &&
    completion_empty
'

#
# flux-keygen (new)
#
test_expect_success 'keygen: completes --name and --meta' '
    run_completion "flux keygen --" &&
    completion_has "--name=" &&
    completion_has "--meta="
'

#
# flux-logger (new)
#
test_expect_success 'logger: completes --severity' '
    run_completion "flux logger --" &&
    completion_has "--severity="
'
test_expect_success 'logger: --severity completes log levels' '
    run_completion "flux logger --severity " &&
    completion_has "info" &&
    completion_has "debug" &&
    completion_has "err"
'

#
# flux-cancel: spot-check existing completion still works
#
test_expect_success 'cancel: completes --states' '
    run_completion "flux cancel --" &&
    completion_has "--states="
'
test_expect_success 'cancel: --states completes state names' '
    run_completion "flux cancel --states " &&
    completion_has "running" &&
    completion_has "pending"
'

#
# Dynamic jobid completion (stubbed)
#
test_expect_success 'cancel: non-option arg completes with jobids' '
    run_completion "flux cancel " &&
    completion_has "ƒ111111111"
'
test_expect_success 'job attach: completes with jobids' '
    run_completion "flux job attach " &&
    completion_has "ƒ111111111"
'

#
# Queue name completion (stubbed)
#
test_expect_success 'jobs: --queue completes queue names' '
    run_completion "flux jobs --queue " &&
    completion_has "default" &&
    completion_has "batch"
'
test_expect_success 'submit: --queue completes queue names' '
    run_completion "flux submit --queue " &&
    completion_has "default" &&
    completion_has "batch"
'

# -----------------------------------------------------------------------
# Dynamic tests: use the real flux instance started by test_under_flux.
# Remove the stub so completion functions call the real flux binary.
# -----------------------------------------------------------------------
unset -f flux

test_expect_success 'setup: submit a regular sleep job' '
    JOBID_REGULAR=$(flux submit --wait-event=start sleep 300) &&
    test_debug "echo regular job: $JOBID_REGULAR"
'
test_expect_success 'setup: submit a batch instance' '
    JOBID_INSTANCE=$(flux batch -n1 --wrap sleep 300) &&
    flux uri --wait $JOBID_INSTANCE >/dev/null &&
    test_debug "echo instance job: $JOBID_INSTANCE"
'

#
# Jobid completion with real jobs
#
test_expect_success 'cancel: completes real active jobids' '
    run_completion "flux cancel " &&
    completion_has "$JOBID_REGULAR" &&
    completion_has "$JOBID_INSTANCE"
'
test_expect_success 'job attach: completes real active jobids' '
    run_completion "flux job attach " &&
    completion_has "$JOBID_REGULAR" &&
    completion_has "$JOBID_INSTANCE"
'
test_expect_success 'exec --jobid: completes real active jobids' '
    run_completion "flux exec --jobid " &&
    completion_has "$JOBID_REGULAR" &&
    completion_has "$JOBID_INSTANCE"
'
test_expect_success 'update: completes real active jobids' '
    run_completion "flux update " &&
    completion_has "$JOBID_REGULAR" &&
    completion_has "$JOBID_INSTANCE"
'

#
# Instance-only completion: proxy/shutdown only show jobs with a URI
#
test_expect_success 'proxy: completes instance jobids only' '
    run_completion "flux proxy " &&
    completion_has "$JOBID_INSTANCE" &&
    completion_lacks "$JOBID_REGULAR"
'
test_expect_success 'shutdown: completes instance jobids only' '
    run_completion "flux shutdown " &&
    completion_has "$JOBID_INSTANCE" &&
    completion_lacks "$JOBID_REGULAR"
'

#
# Coverage check: every flux command must have a completion function or be
# listed in one of the two sets below.
#
test_expect_success 'completion coverage: all commands covered or explicitly excluded' '
    # Restore the real __get_flux_subcommands (overridden at top for static tests)
    eval "$_orig_get_flux_subcommands" &&
    # Populate FLUX_BUILTINS so __get_flux_subcommands enumerates builtin commands
    eval $(bash "${SHARNESS_TEST_SRCDIR}/../etc/completions/get_builtins.sh" \
        "${SHARNESS_TEST_SRCDIR}/../src/cmd/builtin") &&

    # Commands that have completions but are dispatched without a _flux_<cmd>
    # function (handled inline in _flux_core or via a shared function):
    #   submit/run/alloc/batch/bulksubmit -> _flux_submit_commands
    #   getattr/setattr/lsattr            -> _flux_attr
    #   help                              -> inline compgen in _flux_core
    #   python                            -> inline compopt in _flux_core
    special="submit run alloc batch bulksubmit getattr setattr lsattr help python" &&

    # Commands intentionally without completions.  Add a brief reason for each.
    denylist="
        event           \
        fortune         \
        terminus        \
        job-exec-override   \
        job-frobnicator     \
        job-validator       \
        multi-prog          \
        slurm-expiration-sync \
        imp_exec_helper     \
        run-epilog          \
        run-prolog          \
        run-housekeeping    \
        cgroup              \
        env                 \
        heaptrace           \
        lptest              \
        relay               \
        uptime              \
        version             \
        module-exec         \
        module-python-exec  \
    " &&

    missing="" &&
    for cmd in $(__get_flux_subcommands); do
        fn="_flux_${cmd//-/_}"
        # pythonX.Y versioned commands (e.g. python3.13) cannot map to a
        # valid bash function name; they use _flux_python3 via the case dispatch
        [[ $cmd =~ ^python[0-9]+\.[0-9] ]] && fn="_flux_python3"
        if ! declare -f "$fn" >/dev/null 2>&1 &&
           ! _flux_contains_word "$cmd" $special &&
           ! _flux_contains_word "$cmd" $denylist; then
            missing="$missing $cmd"
        fi
    done &&
    test_debug "echo commands with missing completions:${missing:- none}" &&
    test -z "$missing"
'

test_expect_success 'cleanup: cancel test jobs' '
    flux cancel $JOBID_REGULAR $JOBID_INSTANCE
'

test_done
