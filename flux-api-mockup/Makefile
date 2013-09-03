FLUXDIR := /g/g0/dahn/workspace/FLUX/flux-api-mockup

CC	:= gcc
DOT     := dot
DOXYGEN := doxygen
CFLAGS  := -g -O0 -fpic -Wall
LDFLAGS := -L./ -Wl,-rpath=./ -L$(FLUXDIR)/lib -Wl,-rpath=$(FLUXDIR)/lib
TESTOBJS := test_query_pid2LWJId \
	    test_query_LWJId2JobInfo \
	    test_launch_spawn \
	    test_sleeper 

all: libfluxapi.so testcases doxy flux_lwj_states.pdf flux_lwj_states_lmon_actions.pdf 

libfluxapi.so: flux_api_mockup.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LDFLAGS)

flux_api_mockup.o: flux_api_mockup.c flux_api_mockup.h
	$(CC) $(CFLAGS) $< -c -o $@

testcases: $(TESTOBJS) 

test_query_pid2LWJId: test_query_pid2LWJId.o libfluxapi.so 
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lfluxapi

test_query_pid2LWJId.o: test_query_pid2LWJId.c flux_api_mockup.h
	$(CC) $(CFLAGS) $< -c -o $@

test_query_LWJId2JobInfo: test_query_LWJId2JobInfo.o libfluxapi.so 
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lfluxapi

test_query_LWJId2JobInfo.o: test_query_LWJId2JobInfo.c flux_api_mockup.h
	$(CC) $(CFLAGS) $< -c -o $@

test_launch_spawn: test_launch_spawn.o libfluxapi.so 
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lfluxapi

test_launch_spawn.o: test_launch_spawn.c flux_api_mockup.h
	$(CC) $(CFLAGS) $< -c -o $@

test_sleeper : test_sleeper.o
	$(CC) $(CFLAGS) $< -o $@

test_sleeper.o: test_sleeper.c
	$(CC) $(CFLAGS) $< -c -o $@

flux_lwj_states.pdf: flux_lwj_states.dot
	$(DOT) -Tpdf $^ -o $@

flux_lwj_states_lmon_actions.pdf: flux_lwj_states_lmon_actions.dot
	$(DOT) -Tpdf $^ -o $@

doxy: doc/doxy_conf.txt
	$(DOXYGEN) $^

install:
	mkdir -p ./lib ./include
	mkdir -p ./tests
	mv libfluxapi.so ./lib/
	cp flux_api_mockup.h ./include
	cp $(TESTOBJS) ./tests
	chmod u+x ./tests/*
	mv *.pdf doc/

clean:
	rm -rf *~ *.o libfluxapi.so $(TESTOBJS) ./lib ./include ./tests doc/html doc/latex doc/man doc/*.pdf
