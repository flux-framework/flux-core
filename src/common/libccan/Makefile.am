AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS)

EXTRA_DIST = \
	licenses \
	ccan/list/LICENSE \
	ccan/str/LICENSE \
	ccan/str/hex/LICENSE \
	ccan/build_assert/LICENSE \
	ccan/base64/LICENSE \
	ccan/container_of/LICENSE \
	ccan/bitmap/LICENSE \
	ccan/compiler/LICENSE \
	ccan/check_type/LICENSE \
	ccan/array_size/LICENSE \
	ccan/pushpull/LICENSE \
	ccan/ptrint/LICENSE \
	ccan/endian/LICENSE \
	ccan/list/_info \
	ccan/str/_info \
	ccan/str/hex/_info \
	ccan/build_assert/_info \
	ccan/base64/_info \
	ccan/container_of/_info \
	ccan/bitmap/_info \
	ccan/compiler/_info \
	ccan/check_type/_info \
	ccan/array_size/_info \
	ccan/pushpull/_info \
	ccan/ptrint/_info \
	ccan/endian/_info

noinst_LTLIBRARIES = libccan.la
libccan_la_SOURCES = \
	ccan/check_type/check_type.h \
	ccan/pushpull/pull.c \
	ccan/pushpull/pull.h \
	ccan/pushpull/push.c \
	ccan/pushpull/push.h \
	ccan/pushpull/pushpull.h \
	ccan/str/str.c \
	ccan/str/debug.c \
	ccan/str/str.h \
	ccan/str/str_debug.h \
	ccan/str/hex/hex.h \
	ccan/str/hex/hex.c \
	ccan/list/list.c \
	ccan/list/list.h \
	ccan/base64/base64.c \
	ccan/base64/base64.h \
	ccan/bitmap/bitmap.h \
	ccan/bitmap/bitmap.c \
	ccan/endian/endian.h \
	ccan/compiler/compiler.h \
	ccan/ptrint/ptrint.h \
	ccan/build_assert/build_assert.h \
	ccan/container_of/container_of.h \
	ccan/array_size/array_size.h
