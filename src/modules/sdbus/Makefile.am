AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(JANSSON_CFLAGS)
if HAVE_LIBSYSTEMD
AM_CPPFLAGS += \
	$(LIBSYSTEMD_CFLAGS)
endif

noinst_LTLIBRARIES = libsdbus.la

libsdbus_la_SOURCES = \
	main.c
if HAVE_LIBSYSTEMD
libsdbus_la_SOURCES += \
	sdbus.c \
	sdbus.h \
	objpath.c \
	objpath.h \
	message.c \
	message.h \
	interface.c \
	interface.h \
	watcher.c \
	watcher.h \
	subscribe.c \
	subscribe.h \
	connect.c \
	connect.h
endif

test_ldadd = \
	$(builddir)/libsdbus.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(JANSSON_LIBS)
if HAVE_LIBSYSTEMD
test_ldadd += \
	$(LIBSYSTEMD_LIBS)
endif

test_cppflags = \
        $(AM_CPPFLAGS)

test_ldflags = \
        -no-install

TESTS =

if HAVE_LIBSYSTEMD
TESTS += \
	 test_objpath.t \
	 test_message.t
endif

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_objpath_t_SOURCES = test/objpath.c
test_objpath_t_CPPFLAGS = $(test_cppflags)
test_objpath_t_LDADD = $(test_ldadd)
test_objpath_t_LDFLAGS = $(test_ldflags)

test_message_t_SOURCES = test/message.c
test_message_t_CPPFLAGS = $(test_cppflags)
test_message_t_LDADD = $(test_ldadd)
test_message_t_LDFLAGS = $(test_ldflags)
