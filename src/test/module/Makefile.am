AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

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
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(JSON_LIBS) $(ZMQ_LIBS)

parent_la_SOURCES = parent.c
parent_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere
parent_la_LIBADD = $(fluxmod_libadd)

child_la_SOURCES = child.c
child_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere
child_la_LIBADD = $(fluxmod_libadd)
