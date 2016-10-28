AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

check_PROGRAMS = \
	tasyncsock \
	tmunge \
	tbarrier

noinst_SCRIPTS = \
	soak.sh \
	soak-workload.sh \
	valgrind.sh \
	valgrind-workload.sh \
	relnotes.sh

if HAVE_MPI
SUBDIRS = kap
endif

LDADD = $(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(top_builddir)/src/modules/barrier/libflux-barrier.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

EXTRA_DIST = valgrind.supp $(noinst_SCRIPTS)
