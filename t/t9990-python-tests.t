#!/bin/bash

export PYTHONPATH=/home/garlick/proj/flux-core/src/bindings/python:/home/garlick/proj/flux-core/src/bindings/python/pycotap:/home/garlick/proj/flux-core/src/bindings/python/:$PYTHONPATH
export CHECK_BUILDDIR=/home/garlick/proj/flux-core
export FLUX_CONNECTOR_PATH="/home/garlick/proj/flux-core/src/connectors"
export FLUX_PYTHON_DIR="/home/garlick/proj/flux-core/src/bindings/python"

../src/bindings/python/test_commands/test_runner.t


