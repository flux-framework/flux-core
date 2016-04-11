#!/bin/sh
#

test_description='Test KVS zio streams'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} kvs

test_expect_success 'kz: hello world copy in, copy out' '
	echo "hello world" >kztest.1.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil --b 4096 -c - kztest.1 <kztest.1.in &&
	test $(flux kvs dirsize kztest.1) -eq 2 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c kztest.1 - >kztest.1.out &&
	test_cmp kztest.1.in kztest.1.out 
'

test_expect_success 'kz: 128K urandom copy in, copy out' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.2.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -b 4096 -c - kztest.2 <kztest.2.in &&
	test $(flux kvs dirsize kztest.2) -eq 33 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c kztest.2 - >kztest.2.out &&
	test_cmp kztest.2.in kztest.2.out
'

test_expect_success 'kz: write to existing stream (without KZ_FLAGS_TRUNC) fails' '
	echo "hello world" >kztest.3.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c - kztest.3 <kztest.3.in &&
	! ${FLUX_BUILD_DIR}/t/kz/kzutil -c - kztest.3 <kztest.3.in
'

# Write a multi-block stream, then overwrite it with hello world and make
# sure we only have hello world afterwards
test_expect_success 'kz: KZ_FLAGS_TRUNC truncates original content' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.4.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -b 4096 -c - kztest.4 <kztest.4.in &&
	echo "hello world" >kztest.4.in2 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -t -c - kztest.4 <kztest.4.in2 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c kztest.4 - >kztest.4.out &&
	test_cmp kztest.4.in2 kztest.4.out
'

test_expect_success 'kz: KZ_FLAGS_DELAYCOMMIT content gets committed' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.5.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -d -b 4096 -c - kztest.5 <kztest.5.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c kztest.5 - >kztest.5.out &&
	test_cmp kztest.5.in kztest.5.out
'

test_done
