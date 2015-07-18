AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

check_PROGRAMS = \
	tasyncsock \
	tcommit \
	tenc \
	tkvstorture \
	tkvswatch \
	tmunge \
	tpmikvs \
	tbarrier \
	tdisconnect

SUBDIRS = request module
if HAVE_MPI
check_PROGRAMS += mpi_hello
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

tpmikvs_SOURCES = \
	tpmikvs.c

tpmikvs_CPPFLAGS = \
	-I$(top_srcdir)/src/lib/libpmi \
	$(AM_CPPFLAGS)

tpmikvs_LDADD = \
	$(top_builddir)/src/lib/libpmi/libpmi.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

mpi_hello_SOURCES = \
	mpi_hello.c
mpi_hello_CPPFLAGS = \
	$(MPI_CFLAGS)
mpi_hello_LDADD = \
	$(MPI_CLDFLAGS) -lrt $(top_builddir)/src/lib/libpmi/libpmi.la

EXTRA_DIST = valgrind.supp
