#ifndef PERF_PROFILER_H_
#define PERF_PROFILER_H_

#include "globals.h"
#include <string.h>
#include "openmp_profiler.h"
#include <fcntl.h>
#include "treeNode.h"

#define NUM_ENTRIES 1024


extern OpenmpProfiler omp_profiler;
extern std::atomic<unsigned long> node_ctr;

struct CallSiteData {
  const char* cs_filename;
  int cs_line_number;
  
};

struct step_work_data {
    unsigned long incrId;
    TreeNode step_parent;
    size_t work;
    uint64_t lockId;//0 unless associated with a lock
    std::map<size_t, size_t>* region_work;
  };
  
class PerfProfiler{
private:
    //for debugging 
    int ompp_initialized;
    std::ofstream perf_report[NUM_THREADS];

    unsigned int last_allocated[NUM_THREADS];
    struct step_work_data step_work_list[NUM_THREADS][NUM_ENTRIES] = {{{}}};
    int perf_fds[NUM_THREADS];
    std::map<size_t, struct CallSiteData*> regionMap;
    
    void ClearStepWorkListEntry(THREADID threadid);
public:
    PerfProfiler();
    void InitProfiler();
    int perf_event_open_wrapper(struct perf_event_attr *hw_event, pid_t pid,
        int cpu, int group_fd, unsigned long flags);
    long long stop_n_get_count (THREADID threadid);
    void start_count(THREADID threadid);
    void CaptureParallelBegin(THREADID threadid);//
    void CaptureParallelEnd(THREADID threadid);//
    void CaptureMasterBegin(THREADID threadid);//
    void CaptureMasterEnd(THREADID threadid);//
    //single
    void CaptureSingleBeginEnter(THREADID threadid);//
    void CaptureSingleBeginExit(THREADID threadid);//
    void CaptureSingleEndEnter(THREADID threadid);//
    void CaptureSingleEndExit(THREADID threadid);//
    
    void CaptureTaskBegin(THREADID threadid);//
	void CaptureTaskEnd(THREADID threadid);//
    void CaptureImplicitTaskBegin(THREADID threadid);//
    void CaptureImplicitTaskEnd(THREADID threadid);//
    //the following 2 capture calls need to be inserted into the IR as there are no associated ompt event with them
    //void CaptureTaskExecute(THREADID threadid);//
    //void CaptureTaskExecuteReturn(THREADID threadid);//
    void CaptureTaskWaitBegin(THREADID threadid);
    void CaptureTaskWaitEnd(THREADID threadid);
    void CaptureBarrierBegin(THREADID threadid);
    void CaptureBarrierEnd(THREADID threadid);
    void CaptureTaskAllocEnter(THREADID threadid);
    void CaptureTaskAllocExit(THREADID threadid);
    //causal profiling
    void CaptureCausalBegin(THREADID threadid, const char* file, int line, void* return_addr);
    void CaptureCausalEnd(THREADID threadid, const char* file, int line, void* return_addr);
    //critical
    void CaptureWaitCritical(THREADID threadid, uint64_t lockId);
    void CaptureAcquiredCritical(THREADID threadid, uint64_t lockId);
    void CaptureReleaseCritical(THREADID threadid, uint64_t lockId);
    //loop
    void CaptureLoopBeginEnter(THREADID threadid);
    void CaptureLoopBeginExit(THREADID threadid);
    void CaptureLoopEndEnter(THREADID threadid);
    void CaptureLoopEndExit(THREADID threadid);

    void CaptureLoopChunkEnter(THREADID threadid);
    void CaptureLoopChunkExit(THREADID threadid);
    void finishProfile();//
    void serialize(std::ofstream& strm, step_work_data step);
};

#endif