#ifndef GLOBALS_H_
#define GLOBALS_H_

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <assert.h>
#include <map>
#include <vector>
#include <omp.h>
#include <pthread.h>
#include <ompt.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

#include <atomic>

#define NUM_THREADS 16

typedef uint64_t THREADID;

//temporarily add this to stop compile errors
typedef uint64_t ompt_thread_id_t;
typedef uint64_t ompt_task_id_t;
typedef uint64_t ompt_parallel_id_t;
typedef uint64_t ompt_wait_id_t;

//TODO
//move this to util.h?
template <typename T1>
std::ostream& operator << (std::ostream& strm,
const std::vector<T1>& v)
{
    if (v.empty()){
        return strm << "()";
    }
    for (auto it=v.cbegin(); it<v.cend()-1; ++it){
        strm << "(" << *it <<  "), ";
    }
    strm << "(" << *(v.cend()-1) << ")";
    return strm;
}

#endif /* GLOBALS_H_ */
