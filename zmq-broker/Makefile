TOP = $(shell pwd)
MPIMOD = mvapich2-gnu-shmem
#MPIMOD = mvapich2-gnu-psm

CFLAGS = -Werror -Wall -g -fPIC
LDADD = -ljson -lzmq -pthread -luuid -lhiredis
LDADD_C = -ljson -luuid

CMBD_OBJS = cmbd.o zmq.o apisrv.o barriersrv.o syncsrv.o kvssrv.o util.o \
	livesrv.o
CMBUTIL_OBJS = cmbutil.o apicli.o util.o
LIBPMI_OBJS = pmi.o apicli.o util.o

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

run_mpi_hello: mpi_hello
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		export LD_LIBRARY_PATH=$(TOP):$$LD_LIBRARY_PATH;\
		srun ./mpi_hello)

clean:
	rm -f *.o cmbd cmbutil libpmi.so.0 mpi_hello
