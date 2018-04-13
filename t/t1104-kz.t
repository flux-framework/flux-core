#!/bin/sh
#

test_description='Test KVS zio streams'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} kvs

KZCOPY=${FLUX_BUILD_DIR}/t/kz/kzcopy

test_expect_success 'kz: hello world copy in, copy out' '
	echo "hello world" >kztest.1.in &&
	${KZCOPY} - kztest.1 <kztest.1.in &&
	test $(flux kvs dir kztest.1 | wc -l) -eq 2 &&
	${KZCOPY} kztest.1 - >kztest.1.out &&
	test_cmp kztest.1.in kztest.1.out
'

test_expect_success 'kz: 128K urandom copy in, copy out' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.2.in &&
	${KZCOPY} - kztest.2 <kztest.2.in &&
	test $(flux kvs dir kztest.2 | wc -l) -eq 33 &&
	${KZCOPY} kztest.2 - >kztest.2.out &&
	test_cmp kztest.2.in kztest.2.out
'

test_expect_success 'kz: second writer truncates original content' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.4.in &&
	${KZCOPY} - kztest.4 <kztest.4.in &&
	echo "hello world" >kztest.4.in2 &&
	${KZCOPY} - kztest.4 <kztest.4.in2 &&
	${KZCOPY} kztest.4 - >kztest.4.out &&
	test_cmp kztest.4.in2 kztest.4.out
'

test_expect_success 'kz: KZ_FLAGS_DELAYCOMMIT content gets committed' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.5.in &&
	${KZCOPY} -d - kztest.5 <kztest.5.in &&
	${KZCOPY} kztest.5 - >kztest.5.out &&
	test_cmp kztest.5.in kztest.5.out
'

test_expect_success 'kz: KZ_FLAGS_NONBLOCK works for reading' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.6.in &&
	${KZCOPY} - kztest.6 <kztest.6.in &&
	${KZCOPY} --non-blocking kztest.6 - >kztest.6.out &&
	test_cmp kztest.6.in kztest.6.out
'

test_expect_success 'kz: KZ_FLAGS_NONBLOCK works for kz to kz copy' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.7.in &&
	${KZCOPY} - kztest.7 <kztest.7.in &&
	${KZCOPY} --non-blocking kztest.7 kztest.7-cpy &&
	${KZCOPY} --non-blocking kztest.7-cpy - >kztest.7.out &&
	test_cmp kztest.7.in kztest.7.out
'

test_expect_success 'kz: KZ_FLAGS_NOFOLLOW works in non-blocking mode' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.8.in &&
	${KZCOPY} --no-follow - kztest.8 <kztest.8.in &&
	run_timeout 2 \
            ${KZCOPY} --non-blocking --no-follow kztest.8 - >kztest.8.out &&
	test_cmp kztest.8.in kztest.8.out
'

test_expect_success 'kz: KZ_FLAGS_NOFOLLOW works in blocking mode' '
	dd if=/dev/urandom bs=4096 count=32 2>/dev/null >kztest.8.in &&
	${KZCOPY} --no-follow - kztest.8 <kztest.8.in &&
	run_timeout 2 \
            ${KZCOPY} --no-follow kztest.8 - >kztest.8.out &&
	test_cmp kztest.8.in kztest.8.out
'
test_done
