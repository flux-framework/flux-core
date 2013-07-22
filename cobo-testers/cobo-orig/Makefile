CC       = gcc
CFLAGS   = -Werror -Wall -g -fPIC -I./
INCLUDES = $(wildcard *.h)


all: mp_vanilla_cobo_fen mp_vanilla_cobo_be

mp_vanilla_cobo_fen: mp_vanilla_cobo_fen.o cobo.o mp_util.o
	$(CC) -o $@ $^ 

mp_vanilla_cobo_be: mp_vanilla_cobo_be.o cobo.o mp_util.o
	$(CC) -o $@ $^ 

clean:
	rm -rf *~ *breakpoints* *.o mp_vanilla_cobo_fen mp_vanilla_cobo_be

