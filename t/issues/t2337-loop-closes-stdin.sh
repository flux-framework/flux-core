#!/bin/sh -e

# loop connector didn't initialize pollfd correctly, leading
# to an accidental close of STDIN (i.e. fd = 0)

TEST=issue2337
${FLUX_BUILD_DIR}/t/loop/issue2337
