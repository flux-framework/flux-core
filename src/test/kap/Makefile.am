AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS) $(MPI_CFLAGS)

LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(top_builddir)/src/modules/barrier/libflux-barrier.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) \
	$(MPI_CLDFLAGS) $(LIBDL)

check_PROGRAMS = kap

kap_SOURCES = \
	kap.h \
	kap.c \
	kap_opts.h \
	kap_opts.c \
	kap_personality.h \
	kap_personality.c \
	kap_roles.c

document: doc/doxy_conf.txt
	cd doc; doxygen doxy_conf.txt

CLEANFILES = doc/html doc/latex doc/man
