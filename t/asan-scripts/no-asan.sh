#!/bin/sh

# Wrapper to run command without ASAN
#
# assume called from within asan-scripts directory, so we want to
# find the non-ASAN command from $PATH

find_non_asan_command() {
    name=$1
    oldIFS=$IFS
    IFS=:
    for d in $PATH; do
        if [ -x "$d/$name" ]; then
            if echo "$d" | grep -q 'asan-scripts'; then
                continue
            fi
            echo "$d/$name"
            break
        fi
    done
    IFS=$oldIFS
}

unset LD_PRELOAD
unset ASAN_OPTIONS
cmdname=$1
shift
realcmd=$(find_non_asan_command $cmdname)
$realcmd "$@"
