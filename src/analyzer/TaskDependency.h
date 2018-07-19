#ifndef TASK_DEPENDENCY_H_
#define TASK_DEPENDENCY_H_

#include "AGlobals.h"
/// various types of dependency
enum class DependenceType{
    NO_TYPE= 0,
    OUT = 1,
    IN = 2,
    IN_OUT = 3
};

/// Reperents a single dependence
class TaskDependence{
public:
    unsigned long addr;
    DependenceType dep_type; 
    TaskDependence();
    friend std::ostream& operator<<(std::ostream& os, const TaskDependence& td);
};

#endif // TASK_DEPENDENCY_H_