#ifndef TREE_NODE_H_
#define TREE_NODE_H_

#include "globals.h"
#include <tuple>

enum NodeType {
    NO_TYPE = 0,
    ASYNC = 1,
    FINISH = 2,
    STEP = 3
};

class TreeNode{
public:
    std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t> id;
    std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t> parent_id;
    NodeType type;
    NodeType pType;
    unsigned long incrId;
	unsigned long pIncrId;
    unsigned long callSiteId;
    //need these to support additional finish node instead of pending nodes
    NodeType young_ns_child;
    bool finish_flag;    

    TreeNode();
    TreeNode(NodeType node_type);
    void serialize(std::ofstream& strm);
    void serializeTask(std::ofstream& strm, uint64_t task_id);
    friend std::ostream& operator<<(std::ostream& os, const TreeNode& node);
};

#endif
