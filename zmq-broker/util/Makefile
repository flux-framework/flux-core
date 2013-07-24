
LIBUTIL_SOURCES := log.c hostlist.c cbuf.c

libutil.a: $(LIBUTIL_SOURCES:.c=.o)
	ar rcs $@ $^

clean:
	rm -f *.a *.o
