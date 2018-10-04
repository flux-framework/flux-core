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

if ENABLE_PYLINT
#TODO: there must be a better way to do this
# scan flux bindings with pylint, currently fails on any exit but 0
check-local:
	if [ -x "$$( which pylint )" ] ; then  pylint --rcfile=$(top_srcdir)/src/bindings/python/.pylintrc $(top_srcdir)/src/bindings/python/flux; fi
endif

clean-local:
	-rm -f *.pyc *.pyo
	-rm -rf __pycache__

SUBDIRS = core
