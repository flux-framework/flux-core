real_top_srcdir=$(top_srcdir)/../../../
real_top_builddir=$(top_builddir)/../../../
AM_CPPFLAGS = \
	$(WARNING_CFLAGS) -Wno-missing-field-initializers \
	-I$(real_top_srcdir) -I$(real_top_srcdir)/src/include \
	-I$(real_top_srcdir)/src/common/libflux \
	-I$(real_top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(PYTHON_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	-avoid-version -module $(san_ld_zdef_flag) \
	-Wl,-rpath,$(PYTHON_PREFIX)/lib \
	$(CODE_COVERAGE_LIBS)

MAKE_BINDING=$(top_srcdir)/make_binding.py
SUFFIXES = _build.py

common_libs = $(real_top_builddir)/src/common/libflux-core.la \
	      $(real_top_builddir)/src/common/libflux-internal.la \
	      $(ZMQ_LIBS) \
	      $(PYTHON_LDFLAGS)

_build.py.c:
	$(PYTHON) $*_build.py


_core_build.py: $(MAKE_BINDING)
	$(PYTHON) $(MAKE_BINDING) --path $(real_top_srcdir) \
		--search $(real_top_builddir)/src/common/libflux \
		--package _flux \
		--modname _core \
		--add_sub '.*va_list.*|||' \
		--additional_headers src/bindings/python/_flux/callbacks.h \
		--ignore_header 'macros' \
		src/include/flux/core.h

BUILT_SOURCES= _core.c
flyxpy2so_LTLIBRARIES = _core.la
flyxpy2so_PYTHON = __init__.py

dist__core_la_SOURCES = callbacks.h
nodist__core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)
_core_la_DEPENDENCIES = _core_build.py

if HAVE_FLUX_SECURITY
BUILT_SOURCES += _security.c
flyxpy2so_LTLIBRARIES += _security.la
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

lib-copy-vpath: ${flyxpy2so_PYTHON} ${flyxpy2so_LTLIBRARIES}
	-echo Copying libraries to where they can be used by python in-tree
	[ "$(real_top_srcdir)" != "$(real_top_builddir)" ] && cp $(real_top_srcdir)/src/bindings/python/_flux/__init__.py ./; \
	for LIB in ${flyxpy2so_LTLIBRARIES:la=so} ; do \
		test -e .libs/$$LIB && \
		$(LN_S) .libs/$$LIB ./ || true; \
	done

all-local: lib-copy-vpath

# Support VPATH builds
clean-local-vpath:
	[ "$(real_top_srcdir)" != "$(real_top_builddir)" ] && rm -f $(real_top_builddir)/src/bindings/python/_flux/*.py || true

clean-local: clean-local-vpath
	-rm -f *.c *_build.py *.so *.pyc *.pyo
	-rm -rf __pycache__

install-data-hook:
	$(AM_V_at)echo Linking python modules in non-standard location... && \
	  $(MKDIR_P) "$(DESTDIR)$(flyxpy2linkdir)" && \
	  target=$(flyxpy2sodir) && \
	  f=$${target##*/} && \
	  cd "$(DESTDIR)$(flyxpy2linkdir)" && \
	  rm -f $$f && \
	  $(LN_S) $$target .

uninstall-local:
	$(AM_V_at)target=$(flyxpy2sodir) && f=$${target##*/} && \
	  echo "Removing $(flyxpy2linkdir)/$$f" && \
	  rm -rf $(flyxpy2linkdir)/$$f
