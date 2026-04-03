#!/bin/bash
#
# testenv-affected-distros.sh - read changed file paths from stdin (one per
# line, relative to repo root) and output the names of testenv distros that
# may need to be rebuilt.
#
# Rules:
#  src/test/docker/<distro>/* -> output <distro> (direct change)
#  scripts/<file>             -> grep all Dockerfiles for "COPY scripts/<file>",
#                               output the distros that reference it
#
# The 'checks' and 'fluxorama' directories are excluded as they are not
# standalone testenv images.  Output is one distro name per line, sorted and
# deduplicated.
#
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

while IFS= read -r path; do
    case "$path" in
      src/test/docker/*/*)
        distro=${path#src/test/docker/}
        distro=${distro%%/*}
        case "$distro" in
          checks|fluxorama) ;;
          *) echo "$distro" ;;
        esac
        ;;
      scripts/*)
        script=${path#scripts/}
        grep -rl "COPY scripts/${script}" "${SCRIPT_DIR}"/*/Dockerfile 2>/dev/null \
          | sed "s|${SCRIPT_DIR}/||; s|/Dockerfile||"
        ;;
    esac
done | sort -u
