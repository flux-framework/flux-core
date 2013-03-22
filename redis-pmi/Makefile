all: pmi_test

pmi_test: pmi_test.o pmi.o
	$(CC) -o $@ pmi_test.o pmi.o -lhiredis

clean:
	rm -f *.o pmi_test
