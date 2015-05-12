AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
check_LTLIBRARIES = req.la coproc.la

fluxmod_libadd = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '^mod_(main|name)$$'

req_la_SOURCES = req.c
# N.B. "-rpath /nowhere" forces .so build, despite noinst/check
req_la_LDFLAGS = $(fluxmod_ldflags) -rpath /nowhere
req_la_LIBADD = $(fluxmod_libadd)

coproc_la_SOURCES = coproc.c
coproc_la_LDFLAGS = $(fluxmod_ldflags) -rpath /nowhere
coproc_la_LIBADD = $(fluxmod_libadd)

#
# Test proggie
#
check_PROGRAMS = treq

fluxcmd_ldadd = \
        $(top_builddir)/src/modules/live/liblive.la \
        $(top_builddir)/src/modules/modctl/libmodctl.la \
        $(top_builddir)/src/modules/kvs/libkvs.la \
        $(top_builddir)/src/common/libflux/libflux.la \
        $(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
        $(top_builddir)/src/common/liblsd/liblsd.la \
        $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

treq_SOURCES = treq.c
treq_LDADD = $(fluxcmd_ldadd)
