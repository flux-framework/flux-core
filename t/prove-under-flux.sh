#!/bin/sh
prove --merge --timer -j 128 --exec="flux mini run -n1" ./t*.t ./python/t*.py
