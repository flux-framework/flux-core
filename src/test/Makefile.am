AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

check_PROGRAMS = \
	tasyncsock \
	tcommit \
	tenc \
	tkvstorture \
	tkvswatch \
	tmunge

LDADD = $(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBCZMQ) $(LIBZMQ) $(LIBMUNGE) $(LIBPTHREAD)
