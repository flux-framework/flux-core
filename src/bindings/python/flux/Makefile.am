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

.PHONY: lib-copy clean-local-check

lib-copy: ${fluxpy_PYTHON} ${fluxpy_LTLIBRARIES}
	-echo Copying libraries to where they can be used by python in-tree
	for LIB in ${fluxpy_LTLIBRARIES:la=so} ; do \
	    cp -f .libs/$$LIB ./ ; \
	done
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/flux/*.py ./ || true

if ENABLE_PYLINT
#TODO: there must be a better way to do this
# scan flux bindings with pylint, currently fails on any exit but 0
check-local: lib-copy
	if [ -x "$$( which pylint )" ] ; then  pylint --rcfile=$(top_srcdir)/src/bindings/python/.pylintrc ../flux ; fi
endif

all-local: lib-copy


clean-local: clean-local-check
	-rm -f *.c *_build.py *.so *.pyc *.pyo

clean-local-check:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/flux/*.py || true

SUBDIRS = core
