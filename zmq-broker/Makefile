TOP = $(shell pwd)
ifeq ($(shell hostname),jimbo.chaos)
MPIMOD = mvapich2-gnu-shmem
else
MPIMOD = mvapich2-gnu-psm
endif
NTASKS = $(SLURM_JOB_NUM_NODES)  # override me
SRUN = srun --overcommit -n$(NTASKS)

CFLAGS = -Werror -Wall -g -fPIC
LDADD = -ljson -lzmq -lczmq -pthread -luuid -lhiredis
LDADD_C = -ljson -luuid -lzmq -lczmq

CMBD_OBJS = cmbd.o zmq.o util.o log.o plugin.o \
	apisrv.o syncsrv.o barriersrv.o kvssrv.o livesrv.o 
CMBUTIL_OBJS = cmbutil.o apicli.o util.o log.o zmq.o
LIBPMI_OBJS = pmi.o apicli.o util.o log.o zmq.o

all: cmbd cmbutil libpmi.so.0 mpi_hello

cmbd: $(CMBD_OBJS)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD)

cmbutil: $(CMBUTIL_OBJS)
	$(CC) -o $@ $(CMBUTIL_OBJS) $(LDADD_C)

libpmi.so.0: $(LIBPMI_OBJS)
	$(CC) -shared -Wl,-soname,libpmi.so.0 \
	      -o $@ $(LIBPMI_OBJS) -ljson -luuid

mpi_hello: mpi_hello.c
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		mpicc -o $@ mpi_hello.c)
clean:
	rm -f *.o cmbd cmbutil libpmi.so.0 mpi_hello

hello: mpi_hello
	time -p (source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		export LD_LIBRARY_PATH=$(TOP):$$LD_LIBRARY_PATH;\
		$(SRUN) ./mpi_hello)

shello: mpi_hello
	time -p (source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		$(SRUN) ./mpi_hello)

barrier:
	time -p $(SRUN) ./cmbutil -b testbarrier

# Use with single redis configs
clear:
	redis-cli flushall
	redis-cli config resetstat
keys:
	@redis-cli info|grep db0:
info:
	-redis-cli info

# Use with 'nutcracker' config
xclear:
	srun redis-cli -h 127.0.0.1 -p 7777 flushall
	srun redis-cli -h 127.0.0.1 -p 7777 config resetstat
xkeys:
	srun -l redis-cli -h 127.0.0.1 -p 7777 info|grep db0:
xinfo:
	srun -l redis-cli -h 127.0.0.1 -p 7777 info|dshbak -c



