LIBUTIL_SOURCES := log.c hostlist.c util.c zmsg.c hljson.c lsdfatal.c \
		   optparse.c list.c tstat.c

CFLAGS += -DWITH_LSD_NOMEM_ERROR_FUNC -DWITH_LSD_FATAL_ERROR_FUNC

all: libutil.a

libutil.a: $(LIBUTIL_SOURCES:.c=.o)
	ar rcs $@ $^

clean:
	rm -f *.a *.o

.PHONY: all clean
