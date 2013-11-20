
TOP =             $(shell pwd)
LIBDIR =          $(TOP)/lib
EXTRA_CFLAGS +=   -I$(TOP) -I$(TOP)/include -I$(TOP)/util
EXTRA_LDFLAGS +=  -L$(TOP)/lib $(TOP)/util/libutil.a

SUBDIRS :=        wreck
SUBDIRS_POST :=   test

ifeq ($(shell hostname),jimbo.chaos)
MPIMOD = mvapich2-gnu-shmem
else
MPIMOD = mvapich2-gnu-psm
endif

CFLAGS = -Werror -Wall -g -fPIC $(EXTRA_CFLAGS) \
	-DWITH_LSD_NOMEM_ERROR_FUNC -DWITH_LSD_FATAL_ERROR_FUNC \
	-DWANT_RECKLESS_HOSTRANGE_EXPANSION \
	-DPLUGIN_PATH=\"$(TOP)/plugins\"
LDFLAGS = -Wl,-rpath,$(LIBDIR)

export CFLAGS LDFLAGS

LIBUTIL = util/libutil.a
LDADD_CLI = $(EXTRA_LDFLAGS) -ljson -lzmq -lczmq -lcrypto -luuid $(LIBUTIL)
LDADD_SRV = -pthread -rdynamic $(LDADD_CLI) -ldl

COMMON_OBJS = kvscli.o barriercli.o logcli.o cmbdcli.o mrpc.o handle.o
CMBD_OBJS = route.o plugin.o $(COMMON_OBJS) cmbd.o

CMBUTIL_OBJS = cmbutil.o
LIBPMI_OBJS = pmi.o
LIBCMB_OBJS = apicli.o $(COMMON_OBJS)

LIBPMI = $(LIBDIR)/libpmi.so.0
LIBCMB = $(LIBDIR)/libcmb.so.0

PLUGINS = plugins/apisrv.so plugins/syncsrv.so plugins/kvssrv.so \
	plugins/barriersrv.so plugins/livesrv.so plugins/mechosrv.so \
	plugins/logsrv.so plugins/echosrv.so plugins/jobsrv.so \
	plugins/resrcsrv.so plugins/rexecsrv.so plugins/schedsrv.so

BUILD = $(LIBCMB) $(LIBPMI) cmbutil mpi_hello echo cmbd $(PLUGINS)

all: subdirs $(BUILD) subdirs_post

plugins/%.so: %.c
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $<
plugins/rexecsrv.so: wreck/wrexecsrv.c
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $<
plugins/%.so: wreck/%.c
	mkdir -p plugins
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -o $@ $<

subdirs: $(LIBUTIL) $(LIBCMB)
	@for f in $(SUBDIRS); do make -C $$f; done
subdirs_post:
	@for f in $(SUBDIRS_POST); do make -C $$f; done

cmbd: $(CMBD_OBJS)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD_SRV)

cmbutil: $(CMBUTIL_OBJS) $(LIBCMB)
	$(CC) $(LDFLAGS) -o $@ $(CMBUTIL_OBJS) -L$(LIBDIR) -lcmb $(LDADD_CLI)

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
	@make -C util clean
	@for f in $(SUBDIRS) $(SUBDIRS_POST); do make -C $$f clean; done

# header dependencies
OBJS = $(CMBD_OBJS) $(CMBUTIL_OBJS) $(TBASE64_OBJS) \
	$(LIBPMI_OBJS) $(LIBCMB_OBJS)
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
dokvstest_nsquared_cached: test/tpmikvs
	$(SRUN) $< --n-squared

barrier:
	time -p $(SRUN) ./cmbutil -b testbarrier
barrier_torture:
	time -p $(SRUN) ./cmbutil -B 100000

# Use with single redis configs
clear:
	redis-cli flushall
	redis-cli config resetstat
keys:
	@redis-cli info|grep db0:
info:
	-redis-cli info

