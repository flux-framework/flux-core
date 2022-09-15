AM_CPPFLAGS = \
	$(WARNING_CFLAGS) \
	-Wno-missing-field-initializers \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_builddir)/src/common/libflux \
	$(PYTHON_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	-avoid-version \
	-module \
	$(san_ld_zdef_flag) \
	-Wl,-rpath,$(PYTHON_PREFIX)/lib \
	$(CODE_COVERAGE_LIBS) \
	$(top_builddir)/src/common/.libs \
	$(top_builddir)/src/common/libflux/.libs \
	$(top_builddir)/src/common/libdebugged/.libs

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = >/dev/null 2>&1

# This could run after make install
#install-data-hook:
#	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
#	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install
#	touch core.so-built

# TODO, this currently builds the core redundantly, we probably need a check
if HAVE_FLUX_SECURITY
all: core.so-built hostlist.so-built idset.so-built rlist.so-built security.so-built
else
all: core.so-built hostlist.so-built idset.so-built rlist.so-built
endif

# Note that if you provide --flux-root here it doesn't seem to take...
core.so-built: Makefile
	ldconfig
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py build_ext
	touch core.so-built

hostlist.so-built:
	export LD_LIBRARY_PATH=$(top_builddir)/src/common/.libs:$(top_builddir)/src/common/libdebugged/.libs
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py build_ext --hostlist
	touch hostlist.so-built

idset.so-built:
	export LD_LIBRARY_PATH=$(top_builddir)/src/common/.libs:$(top_builddir)/src/common/libdebugged/.libs
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py build_ext --idset
	touch idset.so-built

rlist.so-built:
	export LD_LIBRARY_PATH=$(top_builddir)/src/common/.libs:$(top_builddir)/src/common/libdebugged/.libs
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py build_ext --rlist
	touch rlist.so-built

security.so-built:
	export LD_LIBRARY_PATH=$(top_builddir)/src/common/.libs:$(top_builddir)/src/common/libdebugged/.libs
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py build_ext --security \
	    --security-include=$(FLUX_SECURITY_INCDIR) \
	    --security-src=/code/security
	touch security.so-built

.PHONY: all core.so-built core.so-built idset.so-built rlist.so-built security.so-built install

.PRECIOUS: Makefile

clean:
	rm -rf hostlist.so-built core.so-built idset.so-built rlist.so-built security.so-built

clean-local:
	-rm -f .coverage*

# Currently we don't install with security, since we don't know the paths
install:
	export LD_LIBRARY_PATH=$(top_builddir)/src/common/.libs:$(top_builddir)/src/common/libdebugged/.libs
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install  --hostlist --rlist --idset  # Note to Tom - if I add this it isn't found in the container with python3 --prefix=$(PREFIX)
