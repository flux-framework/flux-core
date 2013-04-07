CFLAGS = -Werror -Wall -g
LDADD = -ljson -lzmq -pthread -luuid -lhiredis
LDADD_C = -ljson -luuid

CMBD_OBJS = cmbd.o zmq.o apisrv.o barriersrv.o syncsrv.o kvssrv.o util.o
CMBUTIL_OBJS = cmbutil.o apicli.o util.o

all: cmbd cmbutil

cmbd: $(CMBD_OBJS)
	$(CC) -o $@ $(CMBD_OBJS) $(LDADD)

cmbutil: $(CMBUTIL_OBJS)
	$(CC) -o $@ $(CMBUTIL_OBJS) $(LDADD_C)
	
clean:
	rm -f *.o cmbd cmbutil
