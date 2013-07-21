TOP = $(shell pwd)
INCDIR = $(TOP)/include
LIBDIR = $(TOP)/lib

ifeq ($(shell hostname),jimbo.chaos)
MPIMOD = mvapich2-gnu-shmem
else
MPIMOD = mvapich2-gnu-psm
endif

CFLAGS = -Werror -Wall -g -fPIC -I$(INCDIR) \
	-DWITH_LSD_NOMEM_ERROR_FUNC -DWITH_LSD_FATAL_ERROR_FUNC
LDFLAGS = -Wl,-rpath,$(LIBDIR)

LDADD_CLI = -ljson -lzmq -lczmq -lcrypto
LDADD_SRV = -pthread -luuid -lhiredis $(LDADD_CLI)

COMMON_OBJS = util.o log.o zmq.o
PLUGIN_OBJS = apisrv.o syncsrv.o kvssrv.o barriersrv.o livesrv.o \
	logsrv.o confsrv.o 
CMBD_OBJS = cmbd.o route.o plugin.o hljson.o hostlist.o lsdfatal.o \
	$(COMMON_OBJS) $(PLUGIN_OBJS)
CMBUTIL_OBJS = cmbutil.o $(COMMON_OBJS)
TBASE64_OBJS = tbase64.o $(COMMON_OBJS)
LIBPMI_OBJS = pmi.o $(COMMON_OBJS)
LIBCMB_OBJS = apicli.o $(COMMON_OBJS)

LIBPMI = $(LIBDIR)/libpmi.so.0
LIBCMB = $(LIBDIR)/libcmb.so.0

BUILD = $(LIBCMB) $(LIBPMI) cmbd cmbutil tbase64 mpi_hello

all: $(BUILD)

cmbd: $(CMBD_OBJS)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD_SRV)

tbase64: $(TBASE64_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TBASE64_OBJS) $(LDADD_CLI)

cmbutil: $(CMBUTIL_OBJS) $(LIBCMB)
	$(CC) $(LDFLAGS) -o $@ $(CMBUTIL_OBJS) -L$(LIBDIR) -lcmb $(LDADD_CLI)

$(LIBPMI): $(LIBPMI_OBJS) $(LIBCMB)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=pmi_version.map \
		-o $@ $(LIBPMI_OBJS) -L$(LIBDIR) -lcmb

$(LIBCMB): $(LIBCMB_OBJS)
	mkdir -p $(shell dirname $@)
	$(CC) -shared -Wl,--version-script=cmb_version.map \
		-o $@ $(LIBCMB_OBJS) $(LDADD_CLI)
	ln -sf $(shell basename $@) $(LIBCMB:.0=)

mpi_hello: mpi_hello.c
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		mpicc $(LDFLAGS) -o $@ mpi_hello.c)
clean:
	rm -f *.o $(BUILD)
	rm -rf $(LIBDIR)/

# header dependencies
OBJS = $(CMBD_OBJS) $(CMBUTIL_OBJS) $(TBASE64_OBJS) \
	$(LIBPMI_OBJS) $(LIBCMB_OBJS)
INCLUDES = $(wildcard *.h)
$(OBJS): $(INCLUDES)

##
## test targets
##

NTASKS = $(SLURM_JOB_NUM_NODES)  # override me
SRUN = srun --overcommit -n$(NTASKS)

hello: mpi_hello
	time -p (source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		export PMI_TRACE=0xff;\
		export LD_LIBRARY_PATH=$(TOP)/lib:$$LD_LIBRARY_PATH;\
		$(SRUN) ./mpi_hello)

shello: mpi_hello
	time -p (source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		$(SRUN) ./mpi_hello)

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

