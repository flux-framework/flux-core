AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = $(PYTHON_CPPFLAGS) \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	"-DFLUX_PYTHON_PATH=\"${pyexecdir}\""

#
# Comms module
#
fluxmod_LTLIBRARIES = pymod.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(JSON_LIBS) $(ZMQ_LIBS)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$'

pymod_la_SOURCES = py_mod.c
pymod_la_LDFLAGS = $(fluxmod_ldflags) $(PYTHON_LDFLAGS)
pymod_la_LIBADD = $(fluxmod_libadd)

fluxpymod_PYTHON = echo.py __init__.py

