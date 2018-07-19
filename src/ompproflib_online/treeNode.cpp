#include "treeNode.h"

TreeNode::TreeNode(){
    type = NodeType::NO_TYPE;
    incrId = 0;
	//pIncrId = 0;
    callSiteId = 0;
    task_id = 0;
    young_ns_child = NodeType::NO_TYPE;
    finish_flag = false;

    object_lock = PTHREAD_MUTEX_INITIALIZER; 
}


TreeNode::TreeNode(NodeType node_type){
    type = node_type;
    incrId = 0;
	//pIncrId = 0;
    callSiteId = 0;
    task_id = 0;
    young_ns_child = NodeType::NO_TYPE;
    finish_flag = false;

    object_lock = PTHREAD_MUTEX_INITIALIZER; 

}

TreeNode::TreeNode(TreeNode* node){
    type = node->type;
    incrId = node->incrId;
    callSiteId = node->callSiteId;
    young_ns_child = node->young_ns_child;
    task_id = node->task_id;
    finish_flag = node->finish_flag;
    parent_ptr = node->parent_ptr;
    object_lock = PTHREAD_MUTEX_INITIALIZER;     
}

//called from perf_profiler for a step node's parent info
std::ostream& operator<<(std::ostream& os, const TreeNode& node){
    //only need the parents Id for step nodes
    os << node.incrId;
    return os;
}

std::ostream& operator<<(std::ostream& os, const TreeNode* node){
    //only need the parents Id for step nodes
    os << node->incrId;
    return os;
}

void TreeNode::serialize(std::ofstream& strm){
    
}

void TreeNode::serializeTask(std::ofstream& strm, uint64_t task_id){
    
}