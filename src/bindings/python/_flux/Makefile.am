AM_CPPFLAGS = \
	$(WARNING_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(PYTHON_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	-avoid-version -module $(san_ld_zdef_flag) \
	$(PYTHON_LDFLAGS) -Wl,-rpath,$(PYTHON_PREFIX)/lib \
	$(CODE_COVERAGE_LIBS)

MAKE_BINDING=$(top_srcdir)/src/bindings/python/make_binding.py
SUFFIXES = _build.py

common_libs = $(top_builddir)/src/common/libflux-core.la \
	      $(top_builddir)/src/common/libflux-internal.la \
	      $(ZMQ_LIBS)

_build.py.c:
	$(PYTHON) $*_build.py


_core_build.py: $(MAKE_BINDING)
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/common/libflux \
		--package _flux \
		--modname _core \
		--add_sub '.*va_list.*|||' \
		--ignore_header 'handle_impl' \
		--ignore_header 'macros' \
		--add_long_sub 'FLUX_SEC_TYPE_ALL.*\n.*\),|||FLUX_SEC_TYPE_ALL = 7,'\
		flux.h

BUILT_SOURCES= _core.c _jsc.c _kvs.c _kz.c
fluxso_LTLIBRARIES = _core.la _kvs.la _jsc.la _kz.la
fluxso_PYTHON = __init__.py

nodist__core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)
_core_la_DEPENDENCIES = _core_build.py

_jsc_build.py: $(top_srcdir)/src/common/libjsc/jstatctl.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/common/libjsc \
		--package _flux \
		--modname _jsc \
		--add_sub '.*va_list.*|||' \
		--include_ffi _core_build \
		jstatctl.h

nodist__jsc_la_SOURCES = _jsc.c
_jsc_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/common/libjsc
_jsc_la_LIBADD = $(common_libs)
_jsc_la_DEPENDENCIES = _jsc_build.py

_kvs_build.py: $(top_srcdir)/src/common/libkvs/kvs.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/common/libkvs \
		--package _flux \
		--modname _kvs \
		--add_sub '.*va_list.*|||' \
		--include_ffi _core_build \
		kvs.h

nodist__kvs_la_SOURCES = _kvs.c
_kvs_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/common/libkvs
_kvs_la_LIBADD = $(common_libs)
_kvs_la_DEPENDENCIES = _kvs_build.py

_kz_build.py: $(top_srcdir)/src/common/libkz/kz.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/common/libkz \
		--package _flux \
		--modname _kz \
		--add_sub '.*va_list.*|||' \
		--include_ffi _core_build \
		--add_sub '.*KZ_FLAGS_DELAYCOMMIT.*,|||KZ_FLAGS_DELAYCOMMIT = 0x0c00' \
		kz.h

nodist__kz_la_SOURCES = _kz.c $(top_srcdir)/src/common/libkz/kz.h
_kz_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/common/libkz
_kz_la_LIBADD = $(common_libs)
_kz_la_DEPENDENCIES = _kz_build.py

if HAVE_FLUX_SECURITY
BUILT_SOURCES += _security.c
fluxso_LTLIBRARIES += _security.la
_security_build.py: $(MAKE_BINDING)
	$(PYTHON) $(MAKE_BINDING) --path $(FLUX_SECURITY_INCDIR)/flux/security \
		--package _flux \
		--modname _security \
		--include_header flux/security/sign.h \
		--add_sub '.*va_list.*|||' \
		sign.h

nodist__security_la_SOURCES = _security.c
_security_la_CPPFLAGS = $(AM_CPPFLAGS) $(FLUX_SECURITY_CFLAGS)
_security_la_LIBADD = $(common_libs) $(FLUX_SECURITY_LIBS)
_security_la_DEPENDENCIES = _security_build.py
endif

.PHONY: lib-copy

lib-copy-vpath: ${fluxso_PYTHON} ${fluxso_LTLIBRARIES}
	-echo Copying libraries to where they can be used by python in-tree
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/_flux/__init__.py ./; \
	for LIB in ${fluxso_LTLIBRARIES:la=so} ; do \
		test -e .libs/$$LIB && \
		$(LN_S) .libs/$$LIB ./ || true; \
	done

all-local: lib-copy-vpath

# Support VPATH builds
clean-local-vpath:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/_flux/*.py || true

clean-local: clean-local-vpath
	-rm -f *.c *_build.py *.so *.pyc *.pyo
	-rm -rf __pycache__
