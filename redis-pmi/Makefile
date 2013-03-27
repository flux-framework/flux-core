TOP = $(shell pwd)

#MPIMOD = mvapich-gnu-shmem
MPIMOD = mvapich2-gnu-shmem
#MPIMOD = openmpi-gnu/1.6
#MPIMOD = openmpi-gnu/1.6-garlick

all: pmi_test mpi_hello libpmi.so.0

pmi_test: pmi_test.o libpmi.so.0
	$(CC) -o $@ pmi_test.o -lpmi

run_pmi_test: pmi_test libpmi.so.0
	(export LD_LIBRARY_PATH=$(TOP):$$LD_LIBRARY_PATH; ./pmi_test)

libpmi.so.0: pmi.c
	$(CC) -c -fPIC pmi.c
	$(CC) -shared -Wl,-soname,libpmi.so.0 \
		-o $@ pmi.o -lhiredis
	
# presumes redis is running
mpi_hello: mpi_hello.c
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		mpicc -o $@ mpi_hello.c)

run_mpi_hello: mpi_hello
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		export LD_LIBRARY_PATH=$(TOP):$$LD_LIBRARY_PATH;\
		srun --redis -n2 ./mpi_hello)

testmod:
	(source /etc/profile.d/modules.sh;\
		module load $(MPIMOD);\
		module list;\
		export LD_LIBRARY_PATH=$(TOP):$$LD_LIBRARY_PATH;\
		ldd mpi_hello)
	
clean:
	rm -f *.o pmi_test mpi_hello libpmi.so.0
