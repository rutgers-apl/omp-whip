#include "perf_profiler.h"

//#define DEBUG 1

inline void PerfProfiler::ClearStepWorkListEntry(THREADID threadid){
    
}

PerfProfiler::PerfProfiler() {
    ompp_initialized = 0;
}

void PerfProfiler::InitProfiler(){
    
    //set perf_fds to zero
    for (unsigned int i=0; i < NUM_THREADS; i++){
        perf_fds[i] = 0;
    }
      //assert_count = 0;
    ompp_initialized = 1;
    start_count(0);
}
int PerfProfiler::perf_event_open_wrapper(struct perf_event_attr *hw_event, pid_t pid,
    int cpu, int group_fd, unsigned long flags)
{
int ret;
ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
    group_fd, flags);
return ret;
}

long long PerfProfiler::stop_n_get_count (THREADID threadid) {
    #if DEBUG
        return 1;
    #else
        //assert_count--;       
        if (perf_fds[threadid] == 0){
            //std::cout << "stop and get count on already closed perf_fd tid= " << threadid << " will return 0 step" << std::endl;
            return 0;
        }
        assert(perf_fds[threadid] != 0); 
        ioctl(perf_fds[threadid], PERF_EVENT_IOC_DISABLE);
        long long count = 0;
        read(perf_fds[threadid], &count, sizeof(long long));
        close(perf_fds[threadid]);
        perf_fds[threadid] = 0;
        return count;
    #endif
}

void PerfProfiler::start_count(THREADID threadid) {
    #if DEBUG
        return;
    #else
        //assert_count++;
        if (perf_fds[threadid]!=0){
            //std::cout << "start count for already started counter! tid = " << threadid << std::endl;
            return;
        }
        assert(perf_fds[threadid]==0);
        struct perf_event_attr pe;
        
        memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.type = PERF_TYPE_HARDWARE; 
        pe.size = sizeof(struct perf_event_attr);
        //pe.config = PERF_COUNT_HW_INSTRUCTIONS;
        pe.config = PERF_COUNT_HW_CPU_CYCLES;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;
        pe.exclude_idle = 1;
        
        int fd;
        fd = perf_event_open_wrapper(&pe, 0, -1, -1, 0);
        if (fd == -1) {
            fprintf(stderr, "Unable to read performance counters. Linux perf event API not supported on the machine. Error number: %d\n", errno);
            exit(EXIT_FAILURE);
        }
        perf_fds[threadid] = fd;
        
        ioctl(fd, PERF_EVENT_IOC_RESET);
        ioctl(fd, PERF_EVENT_IOC_ENABLE);
    #endif
}

void PerfProfiler::updateParent(TreeNode* parent, long long work){
    parent->left_step_work+=work;
    parent->work+=work;
}

void PerfProfiler::updateParentRegionStep(TreeNode* parent, long long work, int speedup=16){
    long long decreased_work = work;
    //how much work has to reduced
    decreased_work = decreased_work - (decreased_work/speedup);
    //remaining work
    decreased_work = work -decreased_work;
    parent->left_step_work+=decreased_work;
    parent->work+=work;
}

void PerfProfiler::CaptureParallelBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
}

void PerfProfiler::CaptureParallelEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    //this will start counting the continuation of the master's thread
    start_count(threadid);
}

void PerfProfiler::CaptureMasterBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);

    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
    start_count(threadid);
}

void PerfProfiler::CaptureMasterEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
    start_count(threadid);
}


void PerfProfiler::CaptureSingleBeginEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
    
}

void PerfProfiler::CaptureSingleBeginExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}

void PerfProfiler::CaptureSingleEndEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureSingleEndExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}
    
void PerfProfiler::CaptureTaskBegin(THREADID threadid){ 
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);    
       
}

void PerfProfiler::CaptureTaskEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
}

void PerfProfiler::CaptureImplicitTaskBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}

void PerfProfiler::CaptureImplicitTaskEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureTaskWaitBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureTaskWaitEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}

void PerfProfiler::CaptureBarrierBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
}
void PerfProfiler::CaptureBarrierEnd(THREADID threadid){
   if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}

void PerfProfiler::CaptureTaskAllocEnter(THREADID threadid){
   if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid); 
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
    
}

void PerfProfiler::CaptureTaskAllocExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    //start counter to measure the continuation of work done for the calling thread
    start_count(threadid);
}


void PerfProfiler::CaptureCausalBegin(THREADID threadid, const char* file, int line, void* return_addr){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
    start_count(threadid);
}

void PerfProfiler::CaptureCausalEnd(THREADID threadid, const char* file, int line, void* return_addr){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);

    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    
    if (regionMap.count((unsigned long)return_addr) == 0) {
        struct CallSiteData* callsiteData = new CallSiteData();
        callsiteData->cs_filename = file;
        callsiteData->cs_line_number = line;
        regionMap.insert(std::pair<size_t, struct CallSiteData*>((unsigned long)return_addr, callsiteData));
    }
    //perform what-if profiling
    updateParentRegionStep(node, count);
    
    start_count(threadid);
}

void PerfProfiler::CaptureWaitCritical(THREADID threadid, uint64_t lockId){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureAcquiredCritical(THREADID threadid, uint64_t lockId){
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}

void PerfProfiler::CaptureReleaseCritical(THREADID threadid, uint64_t lockId){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);
    start_count(threadid);
}

void PerfProfiler::CaptureLoopBeginEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureLoopBeginExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    //start step work for loop construct
    start_count(threadid);
}

void PerfProfiler::CaptureLoopEndEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureLoopEndExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    //start step work continuation of the single thread
    start_count(threadid);
}

void PerfProfiler::CaptureLoopChunkEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    TreeNode* node = omp_profiler.getCurrentParent(threadid+1);
    updateParent(node, count);

}

void PerfProfiler::CaptureLoopChunkExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    //start measuring the next chunk
    start_count(threadid);
}

void PerfProfiler::finishProfile(){
    #if DEBUG
        std::cout << "[!] Running profiler in debug mode" << std::endl;
    #endif 
    long long count = stop_n_get_count(0);
    TreeNode* node = omp_profiler.getCurrentParent(1);
    updateParent(node, count);
    if (!regionMap.empty()){
        std::ofstream report_region_info;
        report_region_info.open("log/region_info.csv");        
        for(auto elem: regionMap){
            auto callSitePtr = elem.second;
            report_region_info << elem.first << "," << callSitePtr->cs_filename << "," << callSitePtr->cs_line_number << std::endl;
        }
        report_region_info.close();
    }

    ompp_initialized = 0;
}

