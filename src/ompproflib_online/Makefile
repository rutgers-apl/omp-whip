ifndef LLVM_HOME
  $(error You must define LLVM_HOME so that the OpenMP library version providing the OMPT API can be found)
endif

# ----------------------------------
# set up OpenMP runtime
# ----------------------------------

LIB_DIR = $(LLVM_HOME)/lib$(LLVM_OMP_LIBDIR_SUFFIX)
LIB = $(LIB_DIR)/libomp.so

LIBS = -Wl,-rpath=$(LIB_DIR) $(LIB) -lm

# ----------------------------------
# include path for ompt.h and omp.h
# ----------------------------------
INCLUDES = -I$(LLVM_HOME)/include  -I.


CC = clang -gdwarf-3 -std=c11
CXX = clang++ -gdwarf-3 -std=c++11

OMPFLAGS = -fopenmp
BASE_FLAGS= -DOMPT_DEBUG -g $(INCLUDES)
FLAGS = $(OMPFLAGS) $(BASE_FLAGS)

all: libompprof

libompprof: ompt-initialize.o ompt-regression.o ompt-tool.o causalProfiler.o openmp_profiler.o perf_profiler.o treeNode.o
	ar r libompprof.a ompt-initialize.o ompt-regression.o ompt-tool.o causalProfiler.o openmp_profiler.o perf_profiler.o treeNode.o

%ompt-regression.o : %ompt-regression.c 
	$(CC)  -c $(BASE_FLAGS) -o $@ $<


%.o : %.c 
	$(CC)  -c $(FLAGS) -o $@ $<

%.o : %.cpp 
	$(CXX) -c $(FLAGS) $(CPPFLAGS) -o $@ $<


clean:
	rm -rf *.o *~

cleanall:
	rm -rf *.o *.a *~
