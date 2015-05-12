AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(LUA_INCLUDE) \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libmrpc \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/modules/broker \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/broker \
	-DWREXECD_BINDIR=\"$(libexecdir)/flux\" \
	-DWRECK_LUA_PATTERN=\"$(sysconfdir)/wreck/lua.d/*.lua\"

wreckscriptsdir = $(sysconfdir)/wreck/lua.d

#
# Comms module
#
fluxmod_LTLIBRARIES = \
	job.la \
	resrc.la \
	wrexec.la

fluxlibexec_PROGRAMS = \
	wrexecd

fluxmod_libadd = \
	$(top_builddir)/src/common/libmrpc/libmrpc.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$' -rpath  /dev/null

job_la_SOURCES = job.c
job_la_LDFLAGS = $(fluxmod_ldflags)
job_la_LIBADD = $(fluxmod_libadd)

wrexec_la_SOURCES = wrexec.c
wrexec_la_LDFLAGS = $(fluxmod_ldflags)
wrexec_la_LIBADD = $(fluxmod_libadd)
wrexec_la_CPPFLAGS = $(AM_CPPFLAGS) -DWREXECD_PATH=\"$(fluxlibexecdir)/wrexecd\"

resrc_la_SOURCES = resrc.c
resrc_la_LDFLAGS = $(fluxmod_ldflags)
resrc_la_LIBADD = $(fluxmod_libadd)

wrexecd_SOURCES = \
	wrexecd.c \
	luastack.c \
	luastack.h

wrexecd_LDADD = \
	$(top_builddir)/src/bindings/lua/libfluxlua.la \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libmrpc/libmrpc.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LUA_LIB) $(LIBPTHREAD)

dist_wreckscripts_SCRIPTS = \
	lua.d/01-env.lua

# XXX: Hack below to force rebuild of unbuilt wrexecd dependencies
#
$(wrexecd_LDADD):
	@cd `dirname $@` && $(MAKE) `basename $@`
