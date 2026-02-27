#!/bin/sh
#
# Common wait utility function for Flux test suite
#

# Default number of iterations (can be overridden)
WAIT_UTIL_DEFAULT_ITERS=50

# Default sleep time in seconds between iterations (can be overridden)
WAIT_UTIL_DEFAULT_SLEEP=0.1

#
# Common utility function to wait for a condition to be met
# The condition is provided as a command to execute
#
# Usage: wait_util [OPTIONS] COMMAND [ARGS...]
#
# Options:
#   -i, --iterations N   Maximum iterations to wait (default: $WAIT_UTIL_DEFAULT_ITERS)
#   -s, --sleep S        Sleep time between iterations (default: $WAIT_UTIL_DEFAULT_SLEEP)
#   -v, --verbose        Print diagnostic messages during waiting
#
# Example usage:
#   wait_util "flux kvs get mykey" # Wait until command succeeds
#   wait_util "test -f myfile"     # Wait until file exists
#   wait_util "grep pattern file"  # Wait until pattern appears in file
#   wait_util -i 100 -s 0.5 "custom_check_command arg1 arg2"
#
wait_util() {
    local iterations=$WAIT_UTIL_DEFAULT_ITERS
    local sleep_time=$WAIT_UTIL_DEFAULT_SLEEP
    local verbose=0

    # Process options
    while [ $# -gt 0 ]; do
        case "$1" in
            -i|--iterations)
                iterations=$2
                shift 2
                ;;
            -s|--sleep)
                sleep_time=$2
                shift 2
                ;;
            -v|--verbose)
                verbose=1
                shift
                ;;
            *)
                break
                ;;
        esac
    done

    # Ensure we have a command to run
    if [ $# -eq 0 ]; then
        echo "Error: wait_util requires a command to execute" >&2
        return 1
    fi

    # The remaining arguments form the command to execute
    local cmd="$@"
    [ $verbose -eq 1 ] && echo "wait_util: running $cmd"

    local i=0
    while [ $i -lt $iterations ]
    do
        # Use eval to properly handle command with arguments
        if eval "$cmd" > /dev/null 2>&1; then
            [ $verbose -eq 1 ] && echo "wait_util: condition met after $i iterations"
            return 0
        fi

        [ $verbose -eq 1 ] && echo "wait_util: waiting... ($i/$iterations)"
        sleep $sleep_time
        i=$((i + 1))
    done

    [ $verbose -eq 1 ] && echo "wait_util: timeout after $iterations iterations"
    return 1
}

