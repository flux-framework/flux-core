#!/bin/sh
#

test_description='Test KVS zio streams'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} kvs

test_expect_success 'kz: hello world copy in, copy out' '
	echo "hello world" >kztest.1.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil --b 4096 -c - kztest.1 <kztest.1.in &&
	test $(flux kvs dir kztest.1 | wc -l) -eq 2 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c kztest.1 - >kztest.1.out &&
	test_cmp kztest.1.in kztest.1.out
'

test_expect_success 'kz: 128K urandom copy in, copy out' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.2.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -b 4096 -c - kztest.2 <kztest.2.in &&
	test $(flux kvs dir kztest.2 | wc -l) -eq 33 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c kztest.2 - >kztest.2.out &&
	test_cmp kztest.2.in kztest.2.out
'

test_expect_success 'kz: second writer truncates original content' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.4.in &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -b 4096 -c - kztest.4 <kztest.4.in &&
	echo "hello world" >kztest.4.in2 &&
	${FLUX_BUILD_DIR}/t/kz/kzutil -c - kztest.4 <kztest.4.in2 &&
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
