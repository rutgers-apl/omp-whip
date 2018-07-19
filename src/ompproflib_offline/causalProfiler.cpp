#pragma GCC push_options
#pragma GCC optimize ("O0")

#include "causalProfiler.h"

OpenmpProfiler omp_profiler;
PerfProfiler perf_profiler;
std::atomic<unsigned long> node_ctr(0);
std::atomic<unsigned long> callSite_ctr(1);

  __attribute__((noinline)) void __causal_begin__(const char* file, int line){
    perf_profiler.CaptureCausalBegin(omp_get_thread_num(), file, line, __builtin_return_address(0));
  }
  
  __attribute__((noinline)) void __causal_end__(const char* file, int line){
    perf_profiler.CaptureCausalEnd(omp_get_thread_num(), file, line, __builtin_return_address(0));
  }

  __attribute__((noinline)) void __ompp_loc_info__(const char* file, int line){
    omp_profiler.updateLoc(omp_get_thread_num(), file, line);
  }

#pragma GCC pop_options