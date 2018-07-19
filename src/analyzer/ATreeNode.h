#ifndef ANALYZERTREE_NODE_H_
#define ANALYZERTREE_NODE_H_


#include <tuple>
#include "AGlobals.h"

enum NodeType {
    NO_TYPE = 0,
    ASYNC = 1,
    FINISH = 2,
    STEP = 3
};

/*todo change member variables to adhere to the style guide. dependece already
adheres to this guide*/
/** This class contains the set of information stored for a node in the 
 * dpst like representation we log during profiling. it also contains 
 * profiling metrics that get computed during analysis
 * */ 
class ATreeNode{
public:
    NodeType type;
    unsigned long incrId;
    unsigned long pIncrId;
    unsigned long parId;
    //profile data 
    long work;
    long cWork;
    long eWork;
    std::map<unsigned long, long> *ssList;
    unsigned long callSiteId;
    //causal
    std::map<unsigned long, long> *regionWork;
    //lock
    uint64_t lockId;    /// Lock identifier used in a step node in a critical section. Assumes that 0 referes to no lock
    long criticalSectionWork;
    //dependence
    task_id_t task_id; /// Ompt task id for Async nodes in the tree that represent a task
    long long dep_c_work;  /// Maximum aggregate critical work from all dependent upon left task siblings
    ATreeNode();
    ATreeNode(NodeType node_type);
    void serialize(std::ofstream& strm);
    friend std::ostream& operator<<(std::ostream& os, const ATreeNode& node);
};

#endif