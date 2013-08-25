
TOP =             $(shell pwd)
LIBDIR =          $(TOP)/lib
EXTRA_CFLAGS +=   -I$(TOP)/include -I$(TOP)/util
EXTRA_LDFLAGS +=  -L$(TOP)/lib $(TOP)/util/libutil.a

SUBDIRS :=        util

ifeq ($(shell hostname),jimbo.chaos)
MPIMOD = mvapich2-gnu-shmem
else
MPIMOD = mvapich2-gnu-psm
endif

CFLAGS = -Werror -Wall -g -fPIC $(EXTRA_CFLAGS) \
	-DWITH_LSD_NOMEM_ERROR_FUNC -DWITH_LSD_FATAL_ERROR_FUNC
LDFLAGS = -Wl,-rpath,$(LIBDIR)

export CFLAGS LDFLAGS

LDADD_CLI = $(EXTRA_LDFLAGS) -ljson -lzmq -lczmq -lcrypto -lutil -luuid
LDADD_SRV = -pthread --lhiredis $(LDADD_CLI)

PLUGIN_OBJS = apisrv.o syncsrv.o kvssrv.o barriersrv.o livesrv.o \
	logsrv.o confsrv.o echosrv.o
CMBD_OBJS = cmbd.o route.o plugin.o lsdfatal.o $(PLUGIN_OBJS)
CMBUTIL_OBJS = cmbutil.o
TBASE64_OBJS = tbase64.o
LIBPMI_OBJS = pmi.o
LIBCMB_OBJS = apicli.o

LIBPMI = $(LIBDIR)/libpmi.so.0
LIBCMB = $(LIBDIR)/libcmb.so.0

BUILD = $(LIBCMB) $(LIBPMI) cmbd cmbutil tbase64 kvstest mpi_hello

all: subdirs $(BUILD)

subdirs:
	@for f in $(SUBDIRS); do make -C $$f; done

cmbd: $(CMBD_OBJS)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD_SRV)

tbase64: $(TBASE64_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TBASE64_OBJS) $(LDADD_CLI)

cmbutil: $(CMBUTIL_OBJS) $(LIBCMB)
	$(CC) $(LDFLAGS) -o $@ $(CMBUTIL_OBJS) -L$(LIBDIR) -lcmb $(LDADD_CLI)

kvstest: kvstest.o
	$(CC) $(LDFLAGS) -o $@ $< -L$(LIBDIR) -lpmi

$(LIBPMI): $(LIBPMI_OBJS) $(LIBCMB)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=pmi_version.map \
		-o $@ $(LIBPMI_OBJS) -L$(LIBDIR) -lcmb $(LDADD_CLI)

$(LIBCMB): $(LIBCMB_OBJS)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=cmb_version.map \
		-o $@ $(LIBCMB_OBJS) $(LDADD_CLI)
	ln -sf $(shell basename $@) $(LIBCMB:.0=)

mpi_hello: mpi_hello.c
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		mpicc $(LDFLAGS) -o $@ $<)


clean:
	rm -f *.o $(BUILD)
	rm -rf $(LIBDIR)/
	@for f in $(SUBDIRS); do make -C $$f clean; done

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

dokvstest: kvstest
	export PMI_TRACE=0xff;\
	$(SRUN) ./kvstest
dokvstest_nsquared: kvstest
	$(SRUN) ./kvstest --n-squared
dokvstest_nsquared_cached: kvstest
	$(SRUN) ./kvstest --n-squared

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

