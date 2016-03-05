CC = gcc
CXX = g++ -std=gnu++0x
CFLAGS = -g -O2 -fPIC
DEPSDIR := hybrid_index/.deps
DEPCFLAGS = -MD -MF $(DEPSDIR)/$*.d -MP
MEMMGR = -ltcmalloc_minimal -lpapi

SNAPPY = /usr/lib/libsnappy.so.1.3.0

all: workload workload_string
#all: workload 
#all: workload_string
#all: arttest


workload.o: workload.cpp microbench.h
	$(CXX) $(CFLAGS) -c -o workload.o workload.cpp

workload: workload.o
	$(CXX) $(CFLAGS) -o workload workload.o $(MEMMGR) -lpthread -lm

workload_string.o: workload_string.cpp microbench.h
	$(CXX) $(CFLAGS) -c -o workload_string.o workload_string.cpp

workload_string: workload_string.o
	$(CXX) $(CFLAGS) -o workload_string workload_string.o $(MEMMGR) -lpthread -lm

arttest.o: arttest.cpp
	$(CXX) $(CFLAGS) -c -o arttest.o arttest.cpp

arttest: arttest.o
	$(CXX) $(CFLAGS) -o arttest arttest.o $(MEMMGR) -lpthread -lm

clean:
	$(RM) workload workload_string arttest *.o *~ *.d
