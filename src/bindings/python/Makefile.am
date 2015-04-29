
SUBDIRS=flux

# Expand python path to find cffi modules during in-tree check

TESTS = \
	test_commands/test_runner.t \
	test_commands/handle.py

TEST_EXTENSIONS = .t .py
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

TESTS_ENVIRONMENT = \
	PYTHONPATH=$(top_builddir)/src/bindings/python:$(top_srcdir)/src/bindings/python/pycotap:$$PYTHONPATH \
	CHECK_BUILDDIR=$(top_builddir)

EXTRA_DIST = pycotap test test_commands make_binding.py

clean-local:
	-rm -f test/*.pyc test_commands/*.pyc

