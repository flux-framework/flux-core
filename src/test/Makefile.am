AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

check_PROGRAMS = \
	tmunge

noinst_SCRIPTS = \
	soak.sh \
	soak-workload.sh \
	relnotes.sh

LDADD = $(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

EXTRA_DIST = $(noinst_SCRIPTS)
