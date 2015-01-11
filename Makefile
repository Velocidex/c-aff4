CXXFLAGS = -std=c++11 -g -Wall -O0
LDFLAGS = -L/usr/lib/x86_64-linux-gnu/
LDLIBS = -lz

SRCS = libaff4.cc zip.cc

all: tests

depend: .depend

.depend: $(SRCS)
	rm -f ./.depend
	$(CXX) $(CXXFLAGS) -MM $^ -MF  ./.depend;

include .depend

tests: stream_test

stream_test: libaff4.o stream_test.cc zip.o

libaff4.o: depend

zip.o: zip.cc zip.h
