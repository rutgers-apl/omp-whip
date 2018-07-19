#ifndef CAUSAL_PROFILER_H
#define CAUSAL_PROFILER_H

#pragma GCC push_options
#pragma GCC optimize ("O0")

#include "globals.h"
#include "openmp_profiler.h"
#include "perf_profiler.h"


extern OpenmpProfiler omp_profiler;
extern PerfProfiler perf_profiler;
extern std::atomic<unsigned long> node_ctr;
extern std::atomic<unsigned long> callSite_ctr;

#define __WHATIF__BEGIN__ __causal_begin__(__FILE__, __LINE__);
#define __WHATIF__END__ __causal_end__(__FILE__, __LINE__);
#define __OMPP_LOC_INFO__ __ompp_loc_info__(__FILE__, __LINE__);

extern "C" {
  
  __attribute__((noinline)) void __causal_begin__(const char* file, int line);

  __attribute__((noinline)) void __causal_end__(const char* file, int line);

  __attribute__((noinline)) void __ompp_loc_info__(const char* file, int line);

}


#pragma GCC pop_options

#endif
