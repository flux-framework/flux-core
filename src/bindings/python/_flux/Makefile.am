AM_CPPFLAGS = \
	$(WARNING_CFLAGS) \
	-Wno-missing-field-initializers \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) \
	$(LIBUUID_CFLAGS) \
	$(PYTHON_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	-avoid-version \
	-module \
	$(san_ld_zdef_flag) \
	-Wl,-rpath,$(PYTHON_PREFIX)/lib \
	$(CODE_COVERAGE_LIBS)

common_libs = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libdebugged/libdebugged.la \
	$(ZMQ_LIBS) $(LIBUUID_LIBS) \
	$(PYTHON_LDFLAGS)

PREPROC_FLAGS = \
	-E '-D__attribute__(...)='

BUILT_SOURCES = \
	_core.c \
	_core_preproc.h \
	_hostlist.c \
	_hostlist_preproc.h \
	_idset.c \
	_idset_preproc.h

fluxpyso_LTLIBRARIES = \
	_core.la \
	_hostlist.la \
	_idset.la

fluxpyso_PYTHON = \
	__init__.py

nodist_fluxbindinginclude_HEADERS = \
	_core_preproc.h \
	_hostlist_preproc.h \
	_idset_preproc.h

EXTRA_DIST = \
	_core_build.py \
	_security_build.py \
	_hostlist_build.py \
	_idset_build.py \
	make_clean_header.py

_core.c: $(srcdir)/_core_build.py _core_preproc.h
	$(PYTHON) $^

_core_clean.h: Makefile
	$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir) \
	  --search $(top_builddir)/src/common/libflux \
	  --additional_headers \
	      src/bindings/python/_flux/callbacks.h \
	      src/common/libdebugged/debugged.h \
	  --output _core_clean.h \
	  src/include/flux/core.h
_core_preproc.h: _core_clean.h
	$(CC) $(PREPROC_FLAGS) _core_clean.h\
	  | sed -e '/^# [0-9]*.*/d' > $@

_hostlist.c: $(srcdir)/_hostlist_build.py _hostlist_preproc.h
	$(PYTHON) $^

_hostlist_clean.h: Makefile
	$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir)/src/common/libhostlist \
	  --output $@ \
	  hostlist.h

_hostlist_preproc.h: _hostlist_clean.h
	$(CC) $(PREPROC_FLAGS) $< \
	  | sed -e '/^# [0-9]*.*/d' > $@

_idset.c: $(srcdir)/_idset_build.py _idset_preproc.h
	$(PYTHON) $^

_idset_clean.h: Makefile
	$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir)/src/common/libidset \
	  --output $@ \
	  idset.h

_idset_preproc.h: _idset_clean.h
	$(CC) $(PREPROC_FLAGS) $< \
	  | sed -e '/^# [0-9]*.*/d' > $@


dist__core_la_SOURCES = callbacks.h
nodist__core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)

nodist__hostlist_la_SOURCES = _hostlist.c
_hostlist_la_LIBADD = $(common_libs)

nodist__idset_la_SOURCES = _idset.c
_idset_la_LIBADD = $(common_libs)

if HAVE_FLUX_SECURITY

BUILT_SOURCES += \
	_security.c\
	_security_preproc.h

fluxpyso_LTLIBRARIES += \
	_security.la

nodist_fluxbindinginclude_HEADERS +=\
	_security_preproc.h

nodist__security_la_SOURCES = \
	_security.c

_security.c: $(srcdir)/_security_build.py _security_preproc.h
	$(PYTHON) $^
_security_clean.h: Makefile
	$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(FLUX_SECURITY_INCDIR)/flux/security \
	  --output _security_clean.h \
	  sign.h
_security_preproc.h: _security_clean.h
	$(CC) $(PREPROC_FLAGS) _security_clean.h \
	  | sed -e '/^# [0-9]*.*/d' > $@

_security_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(FLUX_SECURITY_CFLAGS)
_security_la_LIBADD = \
	$(common_libs) \
	$(FLUX_SECURITY_LIBS)
endif

.PHONY: lib-copy

lib-copy-vpath: ${fluxpyso_PYTHON} ${fluxpyso_LTLIBRARIES}
	-echo Copying libraries to where they can be used by python in-tree
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/_flux/__init__.py ./; \
	for LIB in ${fluxpyso_LTLIBRARIES:la=so} ; do \
		test -e .libs/$$LIB && \
		$(LN_S) .libs/$$LIB ./ || true; \
	done

all-local: lib-copy-vpath

# Support VPATH builds
clean-local-vpath:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/_flux/*.py || true

clean-local: clean-local-vpath
	-rm -f *.c *.so *.pyc *.pyo *_clean.h *_preproc.h
	-rm -rf __pycache__

install-data-hook:
	$(AM_V_at)echo Linking python modules in non-standard location... && \
	  $(MKDIR_P) "$(DESTDIR)$(fluxpylinkdir)" && \
	  target=$(fluxpysodir) && \
	  f=$${target##*/} && \
	  cd "$(DESTDIR)$(fluxpylinkdir)" && \
	  rm -f $$f && \
	  $(LN_S) $$target .

uninstall-local:
	$(AM_V_at)target=$(fluxpysodir) && f=$${target##*/} && \
	  echo "Removing $(fluxpylinkdir)/$$f" && \
	  rm -rf $(fluxpylinkdir)/$$f
