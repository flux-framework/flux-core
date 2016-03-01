#!/bin/sh
#

test_description='Build out of tree using Makefile.inc'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'Makefile.inc is found' '
	make VPATH=${FLUX_SOURCE_DIR}/t/build \
		-f ${FLUX_SOURCE_DIR}/t/build/hello.mk \
		FLUX_MAKEFILE_INC=${FLUX_BUILD_DIR}/etc/Makefile.inc \
		clean
'

test_expect_success 'hello-czmq compiles and runs' '
	make VPATH=${FLUX_SOURCE_DIR}/t/build \
		-f ${FLUX_SOURCE_DIR}/t/build/hello.mk \
		FLUX_MAKEFILE_INC=${FLUX_BUILD_DIR}/etc/Makefile.inc \
		hello_czmq && \
	./hello_czmq
'

test_expect_success 'hello-jsonc compiles and runs' '
	make VPATH=${FLUX_SOURCE_DIR}/t/build \
		-f ${FLUX_SOURCE_DIR}/t/build/hello.mk \
		FLUX_MAKEFILE_INC=${FLUX_BUILD_DIR}/etc/Makefile.inc \
		hello_jsonc && \
	./hello_jsonc
'

test_expect_success 'hello-flux-core compiles and runs' '
	make VPATH=${FLUX_SOURCE_DIR}/t/build \
		-f ${FLUX_SOURCE_DIR}/t/build/hello.mk \
		FLUX_MAKEFILE_INC=${FLUX_BUILD_DIR}/etc/Makefile.inc \
		hello_flux_core && \
	./hello_flux_core
'

test_expect_success 'hello-flux-internal compiles and runs' '
	make VPATH=${FLUX_SOURCE_DIR}/t/build \
		-f ${FLUX_SOURCE_DIR}/t/build/hello.mk \
		FLUX_MAKEFILE_INC=${FLUX_BUILD_DIR}/etc/Makefile.inc \
		hello_flux_internal && \
	./hello_flux_internal
'

test_done
