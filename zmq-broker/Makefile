
TOP =             $(shell pwd)
LIBDIR =          $(TOP)/lib
EXTRA_CFLAGS +=   -I$(TOP) -I$(TOP)/include -I$(TOP)/util -I$(TOP)/zutil
EXTRA_LDFLAGS +=  -L$(TOP)/lib $(TOP)/zutil/libzutil.a $(TOP)/util/libutil.a

SUBDIRS :=        wreck dlua test util zutil

LUA_PKG = $(shell pkg-config --exists lua && echo lua || echo lua5.1)
LUA_CFLAGS = $(shell pkg-config --silence-errors --cflags $(LUA_PKG))
LUA_LIBS = $(shell pkg-config --silence-errors --libs $(LUA_PKG))

CFLAGS = -Werror -Wall -g -fPIC $(EXTRA_CFLAGS) \
	-I/usr/include/czmq \
	-DPLUGIN_PATH=\"$(TOP)/plugins\" \
	-DEXEC_PATH=\"$(TOP)/libexec\" $(LUA_CFLAGS)
LDFLAGS = -Wl,-rpath,$(LIBDIR)

export CFLAGS LDFLAGS LUA_CFLAGS LUA_LIBS

LIBUTIL = util/libutil.a
LIBZUTIL = zutil/libzutil.a
LDADD_CLI = $(EXTRA_LDFLAGS) -ljson -lzmq -lczmq -lutil -lm -lmunge
LDADD_SRV = -pthread -rdynamic $(LDADD_CLI) -ldl

COMMON_OBJS = kvscli.o barriercli.o logcli.o cmbdcli.o mrpc.o handle.o \
	waitqueue.o security.o eventcli.o rankcli.o reduce.o modctlcli.o \
	livecli.o
CMBD_OBJS = plugin.o $(COMMON_OBJS) cmbd.o

FLUX_OBJS = flux.o
LIBPMI_OBJS = pmi.o
LIBCMB_OBJS = apicli.o $(COMMON_OBJS)
LIBRDL_OBJS = rdl.o dlua/json-lua.o util/list.o

LIBPMI = $(LIBDIR)/libpmi.so.0
LIBCMB = $(LIBDIR)/libcmb.so.0
LIBRDL = $(LIBDIR)/librdl.so.0

PLUGINS = plugins/api.so plugins/hb.so plugins/kvs.so \
	plugins/barrier.so plugins/live.so plugins/mecho.so \
	plugins/log.so plugins/echo.so plugins/job.so \
	plugins/resrc.so plugins/rexec.so plugins/sched.so \
	plugins/event.so plugins/rank.so plugins/mon.so \
	plugins/modctl.so

SUBCMDS = libexec/kvs libexec/ping libexec/mecho \
	libexec/stats libexec/barrier libexec/snoop libexec/event \
	libexec/kvswatch libexec/kvsdir libexec/kvstorture \
	libexec/logger libexec/log libexec/info libexec/kvscopy \
	libexec/zio libexec/up libexec/host libexec/keygen libexec/mon \
	libexec/rdltool libexec/mod libexec/peer

BUILD = $(LIBCMB) $(LIBPMI) $(LIBRDL) flux echo cmbd \
	$(PLUGINS) $(SUBCMDS)

VPATH = $(TOP):wreck

all: $(SUBDIRS) $(BUILD)

plugins/%.so: %srv.c $(LIBUTIL)
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBUTIL)
plugins/rexec.so: wreck/wrexecsrv.c $(LIBUTIL)
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBUTIL)
plugins/sched.so: wreck/schedsrv.c $(LIBUTIL) $(LIBRDL)
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBUTIL) -L$(LIBDIR) -lrdl

$(SUBDIRS):
	make -C $@ all

cmbd: $(CMBD_OBJS) $(LIBUTIL) $(LIBZUTIL)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD_SRV)

flux: $(FLUX_OBJS) $(LIBCMB)
	$(CC) $(LDFLAGS) -o $@ $(FLUX_OBJS) $(LDADD_CLI)

libexec/rdltool : flux-rdltool.o $(LIBRDL) $(LIBUTIL)
	mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lrdl -lzmq -lczmq util/libutil.a

libexec/%: flux-%.o $(LIBZUTIL) $(LIBUTIL) $(LIBCMB)
	mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lcmb $(LDADD_CLI) -ldl

$(LIBPMI): $(LIBPMI_OBJS) $(LIBCMB)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=pmi_version.map \
		-o $@ $(LIBPMI_OBJS) -L$(LIBDIR) -lcmb $(LDADD_CLI)
	ln -sf $(shell basename $@) $(@:.0=)

$(LIBCMB): $(LIBCMB_OBJS) $(LIBUTIL) $(LIBZUTIL)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=cmb_version.map \
		-o $@ $(LIBCMB_OBJS) $(LDADD_CLI)
	ln -sf $(shell basename $@) $(@:.0=)

$(LIBRDL): $(LIBRDL_OBJS)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=rdl_version.map \
		-o $@ $(LIBRDL_OBJS) -llua -ljson
	ln -sf $(shell basename $@) $(@:.0=)

$(LIBUTIL): util
$(LIBZUTIL): zutil

echo: echo.o $(LIBUTIL) $(LIBZUTIL) $(LIBCMB)
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lcmb $(LDADD_CLI)

clean:
	rm -f *.o *.so $(BUILD)
	rm -rf $(LIBDIR)/
	rm -rf plugins/
	rm -rf libexec
	@for f in $(SUBDIRS); do make -C $$f clean; done

# subdir dependencies
wreck: util zutil dlua $(LIBCMB)
test:  util $(LIBCMB) $(LIBRDL)
dlua:  util $(LIBCMB)
zutil: util

.PHONY: $(SUBDIRS) all clean
