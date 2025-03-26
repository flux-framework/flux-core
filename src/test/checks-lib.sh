#
# Helper functions for GitHub actions, See:
#
# https://docs.github.com/actions/reference/workflow-commands-for-github-actions
#
#
# Only emit group/endgroup when running under CI, see:
#
# https://docs.github.com/en/actions/reference/environment-variables
#
#
if test "$CI" = "true"; then
  checks_group_start() {
    printf "::group::%s\n" "$1"
  }
  checks_group_end() {
    printf "::endgroup::\n"
  }
else
  checks_group_start() { echo "$@"; }
  checks_group_end()   { echo "$@"; }
fi

#
#  Usage: checks_group DESC COMMANDS...
#
checks_group() {
    local DESC="$1"
    shift 1
    checks_group_start "$DESC"
    eval "$@"
    rc=$?
    checks_group_end
    return $rc
}

#
#  Usage: checks_die MESSAGE COMMANDS...
#
checks_die() {
    local MSG="$1"
    shift 1
    printf "::error::$MSG\n"
    if [ $# -gt 0 ]; then
        eval "$@"
    fi
    exit 1
}
