#include "perf_profiler.h"

//#define DEBUG 1

inline void PerfProfiler::ClearStepWorkListEntry(THREADID threadid){
    if (last_allocated[threadid] < NUM_ENTRIES){
        step_work_list[threadid][last_allocated[threadid]].work = 0;
        step_work_list[threadid][last_allocated[threadid]].lockId = 0;
        if (step_work_list[threadid][last_allocated[threadid]].region_work) {
            delete step_work_list[threadid][last_allocated[threadid]].region_work;
            step_work_list[threadid][last_allocated[threadid]].region_work = nullptr;
        }
    }
}

PerfProfiler::PerfProfiler() {
    ompp_initialized = 0;
}

void PerfProfiler::InitProfiler(){
    for (unsigned int i = 0; i < NUM_THREADS; i++) {
        last_allocated[i] = 0;
      }
    
      for (unsigned int i = 0; i < NUM_THREADS; i++) {
        std::string file_name = "log/step_work_" + std::to_string(i) + ".csv";
        perf_report[i].open(file_name);
      }

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

void PerfProfiler::CaptureParallelBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << "," << 
            step_work_list[threadid][i].step_parent << ","
		    << step_work_list[threadid][i].work;
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }
            //region
            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }

        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
    }
  
  TreeNode node = omp_profiler.getCurrentParent(threadid+1);
  step_work_list[threadid][last_allocated[threadid]].step_parent = node;
  step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
  step_work_list[threadid][last_allocated[threadid]].work += count;
  last_allocated[threadid]++;
  
  ClearStepWorkListEntry(threadid);
}

void PerfProfiler::CaptureParallelEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    //this will start counting the continuation of the master thread
    start_count(threadid);
}

void PerfProfiler::CaptureMasterBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
        
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;

            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
        
    }

    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
    //start step work for master construct
    start_count(threadid);
}
void PerfProfiler::CaptureMasterEnd(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;

            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
    //start step work continuation of master thread
    start_count(threadid);
}


void PerfProfiler::CaptureSingleBeginEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }

        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
        
    }

    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);    

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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
                << step_work_list[threadid][i].work;

            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
        
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;  
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);    
    }
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }
            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
    
      }
    
      TreeNode node = omp_profiler.getCurrentParent(threadid+1);
      step_work_list[threadid][last_allocated[threadid]].step_parent = node;
      step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
      step_work_list[threadid][last_allocated[threadid]].work += count;
      last_allocated[threadid]++;
      ClearStepWorkListEntry(threadid);
}

void PerfProfiler::CaptureTaskWaitBegin(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    

    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
      }
      
      TreeNode node = omp_profiler.getCurrentParent(threadid+1);
      step_work_list[threadid][last_allocated[threadid]].step_parent = node;
      step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
      step_work_list[threadid][last_allocated[threadid]].work += count;
      last_allocated[threadid]++;
      ClearStepWorkListEntry(threadid);
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
    
        if (last_allocated[threadid] == NUM_ENTRIES) {
        //write to file
            for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
                perf_report[threadid] << step_work_list[threadid][i].incrId << ","
                << step_work_list[threadid][i].step_parent << ","
                << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }
            perf_report[threadid] << std::endl;
        }
    
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
      }

      TreeNode node = omp_profiler.getCurrentParent(threadid+1);
      step_work_list[threadid][last_allocated[threadid]].step_parent = node;
      step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
      step_work_list[threadid][last_allocated[threadid]].work += count;
      last_allocated[threadid]++;
      ClearStepWorkListEntry(threadid);
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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
        
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);    

}

void PerfProfiler::CaptureTaskAllocExit(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    start_count(threadid);
}


void PerfProfiler::CaptureCausalBegin(THREADID threadid, const char* file, int line, void* return_addr){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    if (last_allocated[threadid] == NUM_ENTRIES) {
        //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }
            perf_report[threadid] << std::endl;
        }
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
    }

    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    //this will lead to multiple step siblings
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);

    start_count(threadid);
}

void PerfProfiler::CaptureCausalEnd(THREADID threadid, const char* file, int line, void* return_addr){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);

    if (last_allocated[threadid] == NUM_ENTRIES) {
        //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }
            perf_report[threadid] << std::endl;
        }
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        step_work_list[threadid][last_allocated[threadid]].work = 0;
        ClearStepWorkListEntry(threadid);
    }

    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    
    if (regionMap.count((unsigned long)return_addr) == 0) {
        struct CallSiteData* callsiteData = new CallSiteData();
        callsiteData->cs_filename = file;
        callsiteData->cs_line_number = line;
        regionMap.insert(std::pair<size_t, struct CallSiteData*>((unsigned long)return_addr, callsiteData));
    }

    if (step_work_list[threadid][last_allocated[threadid]].region_work == NULL) {
        step_work_list[threadid][last_allocated[threadid]].region_work = new std::map<size_t, size_t>();
        (step_work_list[threadid][last_allocated[threadid]].region_work)->insert( std::pair<size_t, size_t>((unsigned long)return_addr, count) );
    } 
    else {
        std::map<size_t, size_t>* rw_map = step_work_list[threadid][last_allocated[threadid]].region_work;
        if (rw_map->count((unsigned long)return_addr) == 0) {
            rw_map->insert( std::pair<size_t, size_t>((unsigned long)return_addr, count) );
        } 
        else {
            rw_map->at((unsigned long)return_addr) += count;
        }
    }

    //move last_allocated forward
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
    
    start_count(threadid);
}

void PerfProfiler::CaptureWaitCritical(THREADID threadid, uint64_t lockId){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
 
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
        
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);    
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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
        
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    step_work_list[threadid][last_allocated[threadid]].lockId = lockId;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);

    start_count(threadid);
}

void PerfProfiler::CaptureLoopBeginEnter(THREADID threadid){
    if (ompp_initialized==0){
        return;
    }
    long long count = stop_n_get_count(threadid);
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;

            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        //step_work_list[threadid][last_allocated[threadid]].work = 0;
        ClearStepWorkListEntry(threadid);
        
    }

    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
    
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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;

            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        ClearStepWorkListEntry(threadid);
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
    
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
    
    if (last_allocated[threadid] == NUM_ENTRIES) {
    //write to file
        for (unsigned int i = 0; i < NUM_ENTRIES; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
            << step_work_list[threadid][i].work;

            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid]  << "," << it->first << "," << it->second;
                }
            }

            perf_report[threadid] << std::endl;
        }
    
        //updated last_allocated to 0
        last_allocated[threadid] = 0;
        //step_work_list[threadid][last_allocated[threadid]].work = 0;
        ClearStepWorkListEntry(threadid);
    }
    
    TreeNode node = omp_profiler.getCurrentParent(threadid+1);
    step_work_list[threadid][last_allocated[threadid]].step_parent = node;
    step_work_list[threadid][last_allocated[threadid]].incrId = ++node_ctr;
    step_work_list[threadid][last_allocated[threadid]].work += count;
    last_allocated[threadid]++;
    ClearStepWorkListEntry(threadid);
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
    
    for (unsigned int threadid = 0; threadid < NUM_THREADS; threadid++) {
        for (unsigned int i = 0; i < last_allocated[threadid]; i++) {
            perf_report[threadid] << step_work_list[threadid][i].incrId << ","
            << step_work_list[threadid][i].step_parent << ","
                << step_work_list[threadid][i].work;
            
            //lock
            if (step_work_list[threadid][i].lockId !=0){
                perf_report[threadid] << ",L," << step_work_list[threadid][i].lockId;
            }

            if (step_work_list[threadid][i].region_work != NULL) {
                for (std::map<size_t, size_t>::iterator it=step_work_list[threadid][i].region_work->begin();
                it!=step_work_list[threadid][i].region_work->end(); ++it) {
                    perf_report[threadid] << "," << it->first << "," << it->second;
                }
            }
            perf_report[threadid] << std::endl;
        }
    }
      
    TreeNode cur_step = omp_profiler.getCurrentParent(1);

    perf_report[0] << ++node_ctr << "," << cur_step << "," << count;    
    //there cannot be a lock here so don't log lock info 
    
    if (step_work_list[0][last_allocated[0]].region_work != NULL) {
        for (std::map<size_t, size_t>::iterator it=step_work_list[0][last_allocated[0]].region_work->begin();
        it!=step_work_list[0][last_allocated[0]].region_work->end(); ++it) {
            perf_report[0] << "," << it->first << "," << it->second;
        }
    }
    perf_report[0] << std::endl;
    

    for (unsigned int i = 0; i < NUM_THREADS; i++) {
        perf_report[i].close();
    }

    //output region info
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

void PerfProfiler::serialize(std::ofstream& strm, step_work_data step){
    
}
