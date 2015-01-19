CXXFLAGS = -std=c++11 -g -Wall -O0 -I.
LDFLAGS = -L/usr/lib/x86_64-linux-gnu/
LDLIBS = -lz -lyaml-cpp -luuid -lraptor2 -lpcre++

SRCS = libaff4.cc zip.cc data_store.cc aff4_image.cc rdf.cc
OBJS = libaff4.o  zip.o  data_store.o aff4_image.o rdf.o

all: tests aff4imager

depend: .depend

.depend: $(SRCS)
	rm -f ./.depend
	$(CXX) $(CXXFLAGS) -MM $^ -MF  ./.depend;

include .depend

tests: stream_test

stream_test: stream_test.cc $(OBJS)

libaff4.o: depend

aff4imager: aff4imager.cc $(OBJS)

clean:
	rm -f *.o .depend
