#ifndef AGLOBALS_H_
#define AGLOBALS_H_

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <assert.h>
#include <map>
#include <list>
#include <vector>
#include <omp.h>
#include <pthread.h>
#include "ompt.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define NUM_THREADS 16

typedef unsigned int THREADID;
typedef unsigned long task_id_t;

extern pthread_mutex_t report_map_mutex;

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

#endif /* AGLOBALS_H_ */
