#!/usr/bin/env bash

set -e

# This seems to be needed to use our own version of mypy?
mypy --config-file mypy.ini
