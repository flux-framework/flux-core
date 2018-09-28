#
# Fold commands in travis-ci output window with timing
# https://github.com/travis-ci/travis-ci/issues/2285
# and
# https://github.com/travis-ci/travis-build/tree/master/lib/travis/build/bash
#


# Globals for travis_time_start/end:
TRAVIS_TIME=
TRAVIS_TIME_ID=

#  Only emit travis_start/end and travis_time blocks if we're actually
#   running under Travis-CI
#
if test "$TRAVIS" = "true"; then
  travis_time_start() {
    TRAVIS_TIME=$(date +%s%N)
    TRAVIS_TIME_ID="$(printf %08x $((RANDOM * RANDOM)))"
    printf 'travis_time:start:%s\r\033[0K' $TRAVIS_TIME_ID
  }
  travis_time_end() {
    local finish=$(date +%s%N)
    printf 'travis_time:end:%s:start=%s,finish=%s,duration=%s\r\033[0K' \
           $TRAVIS_TIME_ID $TRAVIS_TIME $finish $((finish - TRAVIS_TIME))
  }
  travis_fold_start() {
    printf 'travis_fold:start:%s\r\033[0K\033[33;1m%s\033[0m' "$1" "$2"
  }
  travis_fold_end() {
    printf 'travis_fold:end:%s\r\033[0K' "$1"
  }
else
  travis_time_start() { return 0;}
  travis_time_end()   { return 0;}
  travis_fold_start() { return 0;}
  travis_fold_end()   { return 0;}
fi

travis_fold() {
    local NAME=$1
    local DESC=$2
    shift 2
    travis_fold_start "$NAME" "$DESC"
    travis_time_start
    echo
    eval "$@"
    rc=$?
    travis_time_end
    travis_fold_end "$NAME"
    return $rc
}
