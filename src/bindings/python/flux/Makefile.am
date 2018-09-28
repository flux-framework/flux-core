fluxpy_PYTHON=\
	      __init__.py\
	      kvs.py\
	      wrapper.py\
	      rpc.py\
	      message.py\
	      constants.py\
	      jsc.py\
	      kz.py\
	      sec.py

.PHONY: lib-copy-vpath clean-local-vpath

# Support VPATH builds
lib-copy-vpath: ${fluxpy_PYTHON}
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/flux/*.py ./ || true

if ENABLE_PYLINT
#TODO: there must be a better way to do this
# scan flux bindings with pylint, currently fails on any exit but 0
check-local:
	if [ -x "$$( which pylint )" ] ; then  pylint --rcfile=$(top_srcdir)/src/bindings/python/.pylintrc $(top_srcdir)/src/bindings/python/flux; fi
endif

all-local: lib-copy-vpath

clean-local: clean-local-vpath
	-rm -f *.c *_build.py *.so *.pyc *.pyo
	-rm -rf __pycache__

# Support VPATH builds
clean-local-vpath:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/flux/*.py || true

SUBDIRS = core
