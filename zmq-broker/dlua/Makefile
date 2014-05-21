
all: flux.so cpuset.so libdlua.a

topdir   = $(shell cd .. && pwd)
flux_OBJS += flux-lua.o kvs-lua.o json-lua.o zmsg-lua.o lutil.o
json_test_OBJS += json-lua.o json-test.o
zmsg_test_OBJS += zmsg-lua.o zmsg-test.o json-lua.o lutil.o

libdlua_SOURCES := json-lua.c zmsg-lua.c lutil.c flux-lua.c kvs-lua.c zmsg-lua.o

LIBS     += -lcmb $(topdir)/zutil/libzutil.a $(topdir)/util/libutil.a
LDFLAGS  += -L $(topdir)/lib -Wl,-rpath=$(topdir)/lib
CFLAGS   += -I$(topdir) -I$(topdir)/include


.SUFFIXES: .c .o .so

.c.o:
	$(CC) $(CFLAGS) -fPIC -g -Wall -c $<

flux.so:$(flux_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) $(LIBS)
kvs.so:	$(kvs_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) $(LIBS)
cpuset.so: lua-cpuset.o cpuset-str.o
	$(CC) -shared -o cpuset.so $^ -llua $(LDFLAGS)

jsontest.so: $(json_test_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) -ljson
zmsgtest.so: $(zmsg_test_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) -ljson -lcmb -lutil

libdlua.a : $(libdlua_SOURCES:.c=.o)
	ar rcs $@ $^

check: jsontest.so zmsgtest.so
	@(cd tests && LUA_CPATH=../?.so ./lunit json.lua)
	@(cd tests && LUA_CPATH=../?.so ./lunit test-zmsg.lua)

clean:
	rm -f *.so *.o
