AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms modules
#
check_LTLIBRARIES = \
	parent.la \
	child.la

fluxmod_libadd = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(ZMQ_LIBS)

# N.B. "-rpath /nowhere" forces .so build, despite noinst/check
fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '^mod_(main|name)$$' \
	-rpath /nowhere

parent_la_SOURCES = parent.c
parent_la_LDFLAGS = $(fluxmod_ldflags) 
parent_la_LIBADD = $(fluxmod_libadd)

child_la_SOURCES = child.c
child_la_LDFLAGS = $(fluxmod_ldflags)
child_la_LIBADD = $(fluxmod_libadd)
