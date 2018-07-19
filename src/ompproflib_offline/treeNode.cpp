#include "treeNode.h"

TreeNode::TreeNode(){
    type = NodeType::NO_TYPE;
    incrId = 0;
	pIncrId = 0;
    callSiteId = 0;
    
    young_ns_child = NodeType::NO_TYPE;
    finish_flag = false;
}


TreeNode::TreeNode(NodeType node_type){
    type = node_type;
    incrId = 0;
	pIncrId = 0;
    callSiteId = 0;

    young_ns_child = NodeType::NO_TYPE;
    finish_flag = false;

}

//called from perf_profiler for a step node's parent info
std::ostream& operator<<(std::ostream& os, const TreeNode& node){

    //only need the parents Id for step nodes
    os << node.incrId;
    return os;
}

void TreeNode::serialize(std::ofstream& strm){

    //add parallel Id to profling log
    //id, type, parallel_id, parentId, callSiteId
    strm << incrId << "," << type << "," << std::get<1>(id) << "," << pIncrId << "," << callSiteId << std::endl;

}

void TreeNode::serializeTask(std::ofstream& strm, uint64_t task_id){
    
    /// id, type, parallel_id, parentId, callSiteId, taskIdT
    strm << incrId << "," << type << "," << std::get<1>(id) << "," << pIncrId << "," << callSiteId << "," << task_id << "T" << std::endl;

}