#!/bin/sh -e

flux run -vvv hostname
flux submit -vvv --wait hostname
