#!/bin/sh -e

#
#  Attempt to use handle via RPC within an aux item destructor
#   causes segfault.
#  Also tests that flux_send returns ENOSYS when flux_t handle
#   destruction is in-progress
#
${FLUX_BUILD_DIR}/t/loop/issue2711
