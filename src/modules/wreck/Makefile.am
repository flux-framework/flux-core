AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/modules/broker \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/broker \
	$(LUA_INCLUDE) \
	$(ZMQ_CFLAGS)

wreckscriptsdir = $(sysconfdir)/wreck/lua.d

#
# Comms module
#
fluxmod_LTLIBRARIES = \
	job.la

fluxlibexec_PROGRAMS = \
	wrexecd

fluxmod_libadd = $(top_builddir)/src/common/libflux-core.la \
		 $(top_builddir)/src/common/libflux-internal.la

job_la_SOURCES = job.c rcalc.c rcalc.h wreck_job.c wreck_job.h
job_la_LDFLAGS = $(AM_LDFLAGS) $(fluxmod_ldflags) -module
job_la_LIBADD = $(fluxmod_libadd)

wrexecd_SOURCES = \
	wrexecd.c \
	luastack.c \
	luastack.h

wrexecd_libs = \
	$(top_builddir)/src/bindings/lua/libfluxlua.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libpmi/libpmi.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la

wrexecd_LDFLAGS = \
	$(AM_LDFLAGS) -export-dynamic

wrexecd_LDADD = \
	rcalc.o \
	$(wrexecd_libs) \
	$(ZMQ_LIBS) $(LUA_LIB) $(LIBPTHREAD) $(HWLOC_LIBS)

TESTS = \
	test_wreck_job.t

test_ldadd = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

check_PROGRAMS = $(TESTS)

test_wreck_job_t_SOURCES = test/wreck_job.c
test_wreck_job_t_CPPFLAGS = $(test_cppflags)
test_wreck_job_t_LDADD = \
	$(top_builddir)/src/modules/wreck/wreck_job.o \
	$(test_ldadd)

dist_wreckscripts_SCRIPTS = \
	lua.d/01-env.lua \
	lua.d/timeout.lua \
        lua.d/output.lua \
        lua.d/epilog.lua \
        lua.d/input.lua \
	lua.d/mvapich.lua \
	lua.d/pmi-mapping.lua \
	lua.d/intel_mpi.lua \
	lua.d/openmpi.lua \
	lua.d/spectrum.lua \
	lua.d/cuda_devices.lua
   
# XXX: Hack below to force rebuild of unbuilt wrexecd dependencies
#
$(wrexecd_libs):
	@cd `dirname $@` && $(MAKE) `basename $@`
