fluxpy_PYTHON=\
	      __init__.py\
	      kvs.py\
	      wrapper.py\
	      rpc.py\
	      message.py\
	      constants.py\
	      job.py \
	      mrpc.py \
	      util.py

if HAVE_FLUX_SECURITY
fluxpy_PYTHON += security.py
endif

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
