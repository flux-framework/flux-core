export FLUX_BUILDDIR=$(top_builddir)/../..
export FLUX_SRCDIR=$(top_srcdir)/../..
AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
        -I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
        $(ZMQ_CFLAGS)

#  Always set LUA_PATH such that builddir/?.lua is first so that the
#   build version of fluxometer.lua is found.
#
#  If we're testing with "installed" Flux, then also add path to Lua bindings
#   to LUA_CPATH, so that non-installed test modules can be found (at this
#   time, only lalarm.so)
#
AM_TESTS_ENVIRONMENT = \
	export LUA_PATH="$(builddir)/?.lua;$$LUA_PATH;;;";\
	test -n "$$FLUX_TEST_INSTALLED_PATH" && \
	export LUA_CPATH="$(abs_top_builddir)/src/bindings/lua/.libs/?.so;$$LUA_CPATH;;"; \
	export PYTHONPATH="$(abs_top_builddir):$(abs_top_srcdir):$(top_srcdir)/t/python/tap:$(PYTHON_SITE_PKG):$$PYTHONPATH";

TEST_EXTENSIONS = .t .py
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh
PY_LOG_DRIVER = $(PYTHON) $(top_srcdir)/config/tap-driver.py

clean-local:
	rm -fr trash-directory.* test-results .prove *.broker.log */*.broker.log *.output python/__pycache__

# This list is included in both TESTS and dist_check_SCRIPTS.
TESTSCRIPTS = \
	python/t0001-handle.py \
	python/t0002-wrapper.py \
	python/t0003-barrier.py \
	python/t0004-event.py \
	python/t0005-kvs.py \
	python/t0006-request.py \
	python/t0007-watchers.py \
	python/t0010-job.py \
	python/t0011-mrpc.py \
	python/t0012-futures.py \
	python/t1000-service-add-remove.py

if HAVE_FLUX_SECURITY
TESTSCRIPTS += python/t0009-security.py
endif

# make check runs these TAP tests directly (both scripts and programs)
TESTS = \
	$(TESTSCRIPTS)

EXTRA_DIST= \
	$(T)

dist_check_SCRIPTS = \
	$(TESTSCRIPTS) \
	python/__init__.py \
	python/subflux.py \
	python/tap
