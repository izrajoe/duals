IDIR =./
CC=g++

# -DLEVEL1_DCACHE_LINESIZE detects the cache line size and passes it in as a compiler flag

# Additional options for different builds:

# gcov build
# -fprofile-arcs -ftest-coverage -O0 
# test build
#-DNDEBUG -O3  
#debug build
#-g -rdynamic 
# line by line debug coverage (access via command line: gprof -l)
#-O0 -pg -g 

CFLAGS+=-O3  -ggdb

ODIR=./obj

LIBS=-lpthread -lharness

HARNESS_DIR:=../parHarness/cpp_harness


CFLAGS=-I$(IDIR) -I ./include -I $(HARNESS_DIR) -m32 -Wno-write-strings -fpermissive -pthread -std=c++0x -DLEVEL1_DCACHE_LINESIZE=`getconf LEVEL1_DCACHE_LINESIZE`


_DEPS = MSQueue.hpp TreiberStack.hpp MichaelOrderedSet.hpp Tests.hpp GenericDual.hpp LCRQ.hpp Trivial.hpp FCDualQueue.hpp SimpleRing.hpp SSDualQueue.hpp MPDQ.hpp SPDQ.hpp
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = Tests.o TreiberStack.o MichaelOrderedSet.o GenericDual.o LCRQ.o FCDualQueue.o SSDualQueue.o MPDQ.o SPDQ.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.cpp $(DEPS) 
	@mkdir -p $(@D)
	$(CC) -c -o $@ $< $(CFLAGS)

all: dqs

dqs: $(ODIR)/Main.o  $(OBJ)
	g++ -o $@ $^ $(CFLAGS) -L $(HARNESS_DIR) $(LIBS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ dqs

