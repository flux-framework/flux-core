#
# To bootstap pmgr using PMI
# This will build PMGR for FLUX/CMB if USE_PMI_FOR_PMGR 
# is not given
#

PREFIX     ?= ./ #/usr/local/tools/pmgr_collective
PMGR_INC   := -I../include 
PMGR_LIB   := -L../lib -lpmgr_collective
PMGR_OBJS  := \
	pmgr_collective_common.o \
	pmgr_collective_ranges.o \
	pmgr_collective_client_common.o \
	pmgr_collective_client_mpirun.o \
	pmgr_collective_client_tree.o \
	pmgr_collective_client_slurm.o \
	pmgr_collective_client.o \
	pmgr_collective_mpirun.o 
EXTRA_DEP  := #
LDADD      := #
LDADD_C    := #


ifeq ($(USE_PMI_FOR_PMGR), 1)
OPT        ?= -g -O0 -Wall -DHAVE_PMI -DMPIRUN_PMI_ENABLE=1
LDADD      := -lpmi 
LDADD_C    := -lpmi
else
OPT        ?= -g -O0 -Wall -DHAVE_FLUX_CMB=1 -DMPIRUN_FLUX_CMB_ENABLE=1
CMB_PREFIX := $(shell ./ap `pwd`/../../zmq-broker)
CFLAGS     := -I$(CMB_PREFIX)
EXTRA_DEP  := $(CMB_PREFIX)/apicli.o \
	      $(CMB_PREFIX)/util.o \
	      $(CMB_PREFIX)/log.o \
	      $(CMB_PREFIX)/zmq.o	
LDADD      := -L$(CMB_PREFIX) -Wl,-rpath=$(CMB_PREFIX) \
              -lzmq -lczmq -pthread -luuid -lhiredis -lssl
LDADD_C    := -L$(CMB_PREFIX) -Wl,-rpath=$(CMB_PREFIX) \
              -ljson -luuid -lzmq -lczmq -lssl
endif


all: clean
	mkdir lib
	mkdir include
	cd src && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_common.o pmgr_collective_common.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_ranges.o pmgr_collective_ranges.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_client_common.o pmgr_collective_client_common.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_client_mpirun.o pmgr_collective_client_mpirun.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_client_slurm.o  pmgr_collective_client_slurm.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_client_tree.o   pmgr_collective_client_tree.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_client.o pmgr_collective_client.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o pmgr_collective_mpirun.o pmgr_collective_mpirun.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o cobo_be.o cobo_be.c && \
	  gcc $(OPT) $(CFLAGS) -fPIC -Wall -c -o cobo_fen.o cobo_fen.c && \
	  ar rcs libpmgr_collective.a $(PMGR_OBJS) && \
	  ar rcs libcobo_be.a cobo_be.o $(PMGR_OBJS) && \
	  ar rcs libcobo_fen.a cobo_fen.o $(PMGR_OBJS) && \
	  gcc $(OPT) $(LDFLAGS) -fPIC -shared -Wl,-soname,libpmgr_collective.so.1 -o libpmgr_collective.so.1.0.1 \
		$(PMGR_OBJS) $(EXTRA_DEP) $(LDADD_C) && \
	  gcc $(OPT) $(LDFLAGS) -fPIC -shared -Wl,-soname,libcobo_be.so.1 -o libcobo_be.so.1.0.1 \
		cobo_be.o $(PMGR_OBJS) $(EXTRA_DEP) $(LDADD_C) && \
	  gcc $(OPT) $(LDFLAGS) -fPIC -shared -Wl,-soname,libcobo_fen.so.1 -o libcobo_fen.so.1.0.1 \
		cobo_fen.o $(PMGR_OBJS) $(EXTRA_DEP) $(LDADD_C) && \
	  mv libpmgr_collective.* ../lib/. && \
	  mv libcobo* ../lib && \
	  ln -s libpmgr_collective.so.1.0.1 ../lib/libpmgr_collective.so.1 && \
	  ln -s libpmgr_collective.so.1     ../lib/libpmgr_collective.so && \
	  ln -s libcobo_be.so.1.0.1 ../lib/libcobo_be.so.1 && \
	  ln -s libcobo_be.so.1     ../lib/libcobo_be.so && \
	  ln -s libcobo_fen.so.1.0.1 ../lib/libcobo_fen.so.1 && \
	  ln -s libcobo_fen.so.1     ../lib/libcobo_fen.so && \
	  cp *.h ../include
	cd test && \
	  gcc $(OPT) -o client     client.c     $(PMGR_INC) $(PMGR_LIB) && \
	  gcc $(OPT) -o mpirun_rsh mpirun_rsh.c $(PMGR_INC) $(PMGR_LIB)

install:
	mkdir -p $(PREFIX) && \
	  cp -a include $(PREFIX) && \
	  cp -a lib     $(PREFIX)

clean:
	rm -rf src/*.o src/*~
	rm -rf include
	rm -rf lib
	rm -rf test/*.o test/client test/mpirun_rsh
