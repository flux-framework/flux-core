AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan

fluxconnector_LTLIBRARIES = \
	ssh.la

connector_ldflags = -module $(san_ld_zdef_flag) \
	-export-symbols-regex '^connector_init$$' \
	--disable-static -avoid-version -shared -export-dynamic

ssh_la_SOURCES = \
	ssh/ssh.c
ssh_la_LIBADD = \
       	$(top_builddir)/src/common/librouter/librouter.la \
	$(top_builddir)/src/common/libflux/handle_requeue.lo \
	$(top_builddir)/src/common/libflux/msg_deque.lo \
	$(top_builddir)/src/common/libutil/log.lo \
	$(top_builddir)/src/common/libutil/aux.lo \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libutil/fdutils.lo \
	$(top_builddir)/src/common/libutil/popen2.lo \
	$(top_builddir)/src/common/libutil/fdwalk.lo \
	$(top_builddir)/src/common/libutil/strstrip.lo \
	$(top_builddir)/src/common/libutil/read_all.lo \
	$(top_builddir)/src/common/libyuarel/libyuarel.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(LIBUUID_LIBS)

ssh_la_LDFLAGS = $(connector_ldflags)
