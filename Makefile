

CXXFLAGS = -std=c++11 -g -Wall -O0
LDFLAGS = -L/usr/lib/x86_64-linux-gnu/
LDLIBS = -lz -lyaml-cpp -luuid -lraptor2

SRCS = libaff4.cc zip.cc data_store.cc aff4_image.cc rdf.cc
OBJS = libaff4.o  zip.o  data_store.o aff4_image.o rdf.o

all: tests

depend: .depend

.depend: $(SRCS)
	rm -f ./.depend
	$(CXX) $(CXXFLAGS) -MM $^ -MF  ./.depend;

include .depend

tests: stream_test

stream_test: stream_test.cc $(OBJS)

libaff4.o: depend

clean:
	rm -f *.o .depend
