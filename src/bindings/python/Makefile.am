
SUBDIRS=flux

# Expand python path to find cffi modules during in-tree check

TESTS = \
	test_commands/test_runner.t

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

TESTS_ENVIRONMENT = \
	PYTHONPATH=$(top_builddir)/src/bindings/python:$(top_srcdir)/src/bindings/python/pycotap:$(top_srcdir)/src/bindings/python/:$$PYTHONPATH \
	CHECK_BUILDDIR=$(top_builddir) \
        FLUX_CONNECTOR_PATH="$(top_builddir)/src/connectors"

EXTRA_DIST = pycotap test test_commands make_binding.py

test-coverage: check
	env ${TESTS_ENVIRONMENT} \
	$(PYTHON) -m coverage run --source `pwd`/flux \
				  --concurrency multiprocessing \
				  $(top_srcdir)/src/bindings/python/test_commands/test_runner.t
	$(PYTHON) -m coverage combine
	$(PYTHON) -m coverage report -m \
				  --omit '*_*_build.py,*pycotap*,*test/*,*test_commands/*'

clean-local:
	-rm -f test/*.pyc test_commands/*.pyc
	-rm -f .coverage*

dist-hook:
	-rm -f test/*.pyc test_commands/*.pyc
	-rm -f test_commands/*.log test_commands/*.trs
