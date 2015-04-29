
AM_CFLAGS =	@GCCWARN@ -I$(top_srcdir) -I$(top_srcdir)/src/include -I$(top_srcdir)/src/common/libflux \
		$(JSON_CFLAGS) $(PYTHON_CPPFLAGS) -O2
AM_LDFLAGS = -avoid-version -module -Wl,--no-undefined  $(PYTHON_LDFLAGS) -Wl,-rpath,$(PYTHON_PREFIX)/lib

MAKE_BINDING=$(top_srcdir)/src/bindings/python/make_binding.py
SUFFIXES = _build.py

common_libs = $(top_builddir)/src/common/libflux-core.la  $(JSON_LIBS) 

_build.py.c:
	$(PYTHON) $*_build.py


_core_build.py: $(MAKE_BINDING)
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/common/libflux \
	  			  --package flux \
	                          --modname _core \
				  --add_sub '.*va_list.*|||' \
				  --ignore_header 'handle_impl' \
				  --add_long_sub 'FLUX_SEC_TYPE_ALL.*\n.*\),|||FLUX_SEC_TYPE_ALL = 7,'\
				  flux.h

fluxpy_LTLIBRARIES = _core.la _barrier.la _mrpc.la _kvs.la _jsc.la

_core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)
_core_la_DEPENDENCIES = _core_build.py

_jsc_build.py: $(top_srcdir)/src/modules/libjsc/jstatctl.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/modules/libjsc \
	  			  --package flux \
	                          --modname _jsc \
				  --include_ffi _core_build \
				  jstatctl.h

_jsc_la_SOURCES = _jsc.c
_jsc_la_CPPFLAGS = -I$(top_srcdir)/src/modules/libjsc
_jsc_la_LIBADD = $(top_builddir)/src/modules/libjsc/libjsc.la $(common_libs)
_jsc_la_DEPENDENCIES = _jsc_build.py

_barrier_build.py: $(top_srcdir)/src/modules/barrier/barrier.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/modules/barrier \
	  			  --package flux \
	                          --modname _barrier \
				  --include_ffi _core_build \
				  barrier.h

_barrier_la_SOURCES = _barrier.c
_barrier_la_CPPFLAGS = -I$(top_srcdir)/src/modules/barrier
_barrier_la_LIBADD = $(top_builddir)/src/modules/barrier/libbarrier.la $(common_libs)
_barrier_la_DEPENDENCIES = _barrier_build.py


_mrpc_build.py: $(top_srcdir)/src/modules/libmrpc/mrpc.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/modules/libmrpc \
	  			  --package flux \
	                          --modname _mrpc \
				  --include_ffi _core_build \
				  mrpc.h

_mrpc_la_SOURCES = _mrpc.c
_mrpc_la_CPPFLAGS = -I$(top_srcdir)/src/modules/libmrpc
_mrpc_la_LIBADD = $(top_builddir)/src/modules/libmrpc/libmrpc.la $(common_libs)
_mrpc_la_DEPENDENCIES = _mrpc_build.py

_kvs_build.py: $(top_srcdir)/src/modules/kvs/kvs.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/modules/kvs \
	  			  --package flux \
	                          --modname _kvs \
				  --include_ffi _core_build \
				  kvs.h

_kvs_la_SOURCES = _kvs.c
_kvs_la_CPPFLAGS = -I$(top_srcdir)/src/modules/kvs
_kvs_la_LIBADD = $(top_builddir)/src/modules/kvs/libkvs.la $(common_libs)
_kvs_la_DEPENDENCIES = _kvs_build.py

fluxpy_PYTHON=\
	      __init__.py\
	      mrpc.py\
	      kvs.py\
	      wrapper.py\
	      json_c.py\
	      rpc.py\
	      message.py\
	      constants.py\
	      barrier.py\
	      jsc.py\
	      sec.py

#TODO: there must be a better way to do this
check-local:
	-echo Copying libraries to where they can be used by python for check
	cp -f .libs/*.so ./
	[ ! -e ./__init__.py ] && cp -s $(top_srcdir)/src/bindings/python/flux/*.py ./ || true

clean-local: clean-local-check
	-rm -f *.c *_build.py *.so *.pyc *.pyo

.PHONY: clean-local-check
clean-local-check:
	find -name '*.py' -and -type s -print0 | xargs -0 rm -f

SUBDIRS = core
