
TOP =             $(shell pwd)
LIBDIR =          $(TOP)/lib
EXTRA_CFLAGS +=   -I$(TOP) -I$(TOP)/include -I$(TOP)/util -I$(TOP)/zutil
EXTRA_LDFLAGS +=  -L$(TOP)/lib $(TOP)/zutil/libzutil.a $(TOP)/util/libutil.a

SUBDIRS :=        wreck dlua
SUBDIRS_POST :=   test

ifeq ($(shell hostname),jimbo.chaos)
MPIMOD = mvapich2-gnu-shmem
else
MPIMOD = mvapich2-gnu-psm
endif

CFLAGS = -Werror -Wall -g -fPIC $(EXTRA_CFLAGS) \
	-DWITH_LSD_NOMEM_ERROR_FUNC -DWITH_LSD_FATAL_ERROR_FUNC \
	-DWANT_RECKLESS_HOSTRANGE_EXPANSION \
	-DPLUGIN_PATH=\"$(TOP)/plugins\" \
	-DEXEC_PATH=\"$(TOP)/libexec\"
LDFLAGS = -Wl,-rpath,$(LIBDIR)

export CFLAGS LDFLAGS

LIBUTIL = util/libutil.a
LIBZUTIL = zutil/libzutil.a
LDADD_CLI = $(EXTRA_LDFLAGS) -ljson -lzmq -lczmq -lcrypto -luuid -lutil -lm \
	-lsodium
LDADD_SRV = -pthread -rdynamic $(LDADD_CLI) -ldl

COMMON_OBJS = kvscli.o barriercli.o logcli.o cmbdcli.o mrpc.o handle.o \
	waitqueue.o
CMBD_OBJS = route.o plugin.o $(COMMON_OBJS) cmbd.o

FLUX_OBJS = flux.o
LIBPMI_OBJS = pmi.o
LIBCMB_OBJS = apicli.o $(COMMON_OBJS)

LIBPMI = $(LIBDIR)/libpmi.so.0
LIBCMB = $(LIBDIR)/libcmb.so.0

PLUGINS = plugins/apisrv.so plugins/syncsrv.so plugins/kvssrv.so \
	plugins/barriersrv.so plugins/livesrv.so plugins/mechosrv.so \
	plugins/logsrv.so plugins/echosrv.so plugins/jobsrv.so \
	plugins/resrcsrv.so plugins/rexecsrv.so plugins/schedsrv.so

SUBCMDS = libexec/kvs libexec/ping libexec/mecho libexec/route \
	libexec/stats libexec/barrier libexec/snoop libexec/event \
	libexec/kvswatch libexec/kvsdir libexec/kvstorture \
	libexec/logger libexec/log libexec/info libexec/kvscopy \
	libexec/zio libexec/up libexec/host libexec/keygen

BUILD = $(LIBCMB) $(LIBPMI) flux mpi_hello echo cmbd \
	$(PLUGINS) $(SUBCMDS)

all: subdirs $(BUILD) subdirs_post

plugins/%.so: %.c
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBUTIL)
plugins/rexecsrv.so: wreck/wrexecsrv.c
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBUTIL)
plugins/%.so: wreck/%.c
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBUTIL)

subdirs: $(LIBUTIL) $(LIBZUTIL) $(LIBCMB)
	@for f in $(SUBDIRS); do make -C $$f; done
subdirs_post:
	@for f in $(SUBDIRS_POST); do make -C $$f; done

cmbd: $(CMBD_OBJS)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD_SRV)

flux: $(FLUX_OBJS) $(LIBCMB)
	$(CC) $(LDFLAGS) -o $@ $(FLUX_OBJS) $(LDADD_CLI)
libexec/%: flux-%.o
	mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lcmb $(LDADD_CLI)

$(LIBPMI): $(LIBPMI_OBJS) $(LIBCMB)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=pmi_version.map \
		-o $@ $(LIBPMI_OBJS) -L$(LIBDIR) -lcmb $(LDADD_CLI)
	ln -sf $(shell basename $@) $(@:.0=)

$(LIBCMB): $(LIBCMB_OBJS) $(LIBUTIL)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=cmb_version.map \
		-o $@ $(LIBCMB_OBJS) $(LDADD_CLI)
	ln -sf $(shell basename $@) $(@:.0=)

$(LIBUTIL):
	@make -C util
$(LIBZUTIL):
	@make -C zutil

mpi_hello: mpi_hello.c
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		mpicc $(LDFLAGS) -o $@ $<)

echo: echo.o
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lcmb $(LDADD_CLI)

clean:
	rm -f *.o *.so $(BUILD)
	rm -rf $(LIBDIR)/
	rm -rf plugins/
	rm -rf libexec
	@make -C util clean
	@make -C zutil clean
	@for f in $(SUBDIRS) $(SUBDIRS_POST); do make -C $$f clean; done

# header dependencies
OBJS = $(CMBD_OBJS) $(FLUX_OBJS) $(LIBPMI_OBJS) $(LIBCMB_OBJS)
INCLUDES = $(wildcard *.h include/*.h)
$(OBJS): $(INCLUDES)

##
## test targets
##

NTASKS = $(SLURM_JOB_NUM_NODES)  # override me
SRUN = srun --overcommit -n$(NTASKS)

hello: mpi_hello
	time -p (source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		$(SRUN) ./mpi_hello)

dokvstest: test/tpmikvs
	export PMI_TRACE=0xff;\
	$(SRUN) $<
dokvstest_nsquared: test/tpmikvs
	$(SRUN) $< --n-squared
