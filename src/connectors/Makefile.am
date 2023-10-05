AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan

fluxconnector_LTLIBRARIES = \
	local.la \
	shmem.la \
	ssh.la

connector_ldflags = -module $(san_ld_zdef_flag) \
	-export-symbols-regex '^connector_init$$' \
	--disable-static -avoid-version -shared -export-dynamic

local_la_SOURCES = \
	local/local.c
local_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
local_la_LDFLAGS = $(connector_ldflags)

shmem_la_SOURCES = \
	shmem/shmem.c
shmem_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(ZMQ_CFLAGS)
shmem_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(ZMQ_LIBS)
shmem_la_LDFLAGS = $(connector_ldflags)

ssh_la_SOURCES = \
	ssh/ssh.c
ssh_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
ssh_la_LDFLAGS = $(connector_ldflags)
