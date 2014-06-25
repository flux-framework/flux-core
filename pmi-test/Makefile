TOP = $(shell cd .. && pwd)

# Uncomment for "native" MPI package
MPIMOD = mvapich2-gnu-shmem  # Single node standalone
#MPIMOD = mvapich2-gnu-psm   # Hype
#MPIMOD = mvapich2-gnu-ofa   # IPA

PMI_CFLAGS = -I$(TOP)/zmq-broker/include
PMI_LIBS = -Wl,-rpath,$(TOP)/zmq-broker/lib -lpmi

CFLAGS = $(PMI_CFLAGS)

BUILD = mpi_hello tpmikvs

all: $(BUILD)

mpi_hello: mpi_hello.c
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		mpicc $(PMI_LIBS) -o $@ $<)

tpmikvs: tpmikvs.c
	$(CC) $(PMI_CFLAGS) -o $@ $< $(PMI_LIBS)

clean:
	rm -f $(BUILD)

##
## test targets
##

NTASKS = $(SLURM_JOB_NUM_NODES)  # override me
SRUN = srun --overcommit -n$(NTASKS)

hello: mpi_hello
	time -p (source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		$(SRUN) ./mpi_hello)

dokvstest: tpmikvs
	export PMI_TRACE=0xff;\
	$(SRUN) $<
dokvstest_nsquared: tpmikvs
	$(SRUN) $< --n-squared

.PHONY: all clean
