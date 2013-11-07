
all: flux.so kvs.so

topdir   = $(shell cd .. && pwd)
flux_OBJS += flux-lua.o json-lua.o lutil.o
kvs_OBJS += kvs-lua.o json-lua.o lutil.o
json_test_OBJS += json-lua.o json-test.o
zmsg_test_OBJS += zmsg-lua.o zmsg-test.o json-lua.o lutil.o

LIBS     += -lcmb
LDFLAGS  += -L $(topdir)/lib -Wl,-rpath=$(topdir)/lib
CFLAGS   += -I$(topdir) -I$(topdir)/include

.SUFFIXES: .c .o .so

.c.o:
	$(CC) $(CFLAGS) -fPIC -g -Wall -c $<

flux.so:$(flux_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) $(LIBS)
kvs.so:	$(kvs_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) $(LIBS)

jsontest.so: $(json_test_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) -ljson
zmsgtest.so: $(zmsg_test_OBJS)
	$(CC) -shared -o $*.so $^ $(LDFLAGS) -ljson -lcmb -lutil
check: jsontest.so zmsgtest.so
	@(cd tests && LUA_CPATH=../?.so ./lunit json.lua)
	@(cd tests && LUA_CPATH=../?.so ./lunit test-zmsg.lua)

clean:
	rm -f *.so *.o
