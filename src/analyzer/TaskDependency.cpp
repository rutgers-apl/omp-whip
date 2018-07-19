#include "TaskDependency.h"

TaskDependence::TaskDependence(){
    addr = 0;
    dep_type = DependenceType::NO_TYPE; 
}

std::ostream& operator<<(std::ostream& os, const TaskDependence& td){
    os << std::hex << td.addr << std::dec << "," << static_cast<int>(td.dep_type);
    return os;
}
