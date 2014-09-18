AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(MPI_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

LDADD = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
        $(top_builddir)/src/common/libflux/libflux.la \
        $(top_builddir)/src/common/libutil/libutil.la \
        $(top_builddir)/src/common/liblsd/liblsd.la \
        $(JSON_LIBS) $(LIBCZMQ) $(LIBZMQ) $(LIBMUNGE) $(LIBPTHREAD) $(LIBM)
	$(MPI_CLDFLAGS)
		

check_PROGRAMS = kap

kap_SOURCES = \
	kap.c \
	kap_opts.c \
	kap_personality.c \
	kap_roles.c

document: doc/doxy_conf.txt
	cd doc; doxygen doxy_conf.txt

CLEANFILES = doc/html doc/latex doc/man
