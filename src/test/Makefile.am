AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

check_PROGRAMS = \
	tasyncsock \
	tcommit \
	tmunge \
	tbarrier \
	tdisconnect

SUBDIRS = request module
if HAVE_MPI
SUBDIRS += kap
endif

LDADD = $(top_builddir)/src/modules/libzio/libzio.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/barrier/libbarrier.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

EXTRA_DIST = valgrind.supp
