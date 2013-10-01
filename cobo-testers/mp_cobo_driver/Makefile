TOP = $(shell pwd)
ifeq ($(shell hostname),jimbo.chaos)
MPIMOD = mvapich2-gnu-shmem
else
MPIMOD = mvapich2-gnu-psm
endif
NTASKS = $(SLURM_JOB_NUM_NODES)  # override me
SRUN   = srun --overcommit -n$(NTASKS)

CMB_DIR = ../../zmq-broker#
CFLAGS  = -Werror -Wall -g -fPIC -I$(CMB_DIR)/include -I./ -I../cobo-cmb/include
INCLUDES = $(wildcard *.h)

###########################################################
#
#                         RULES
#
###########################################################


all: mp_cobo_fen mp_cobo_be

mp_cobo_fen: mp_cobo_fen.o mp_util.o 
	$(CC) -o $@ $^ -Wl,-rpath=../cobo-cmb/lib -L../cobo-cmb/lib -lcobo_fen

mp_cobo_be: mp_cobo_be.o mp_util.o 
	$(CC) -o $@ $^  -Wl,-rpath=../cobo-cmb/lib -L../cobo-cmb/lib -lcobo_be

clean:
	rm -f *.o mp_cobo_fen mp_cobo_be *~ *breakpoints*

$(OBJS): $(INCLUDES)

