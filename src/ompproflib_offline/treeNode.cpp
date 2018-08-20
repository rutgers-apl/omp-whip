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
    /*
    os << node.incrId << ", " << std::get<0>(node.id) << ", " << std::get<1>(node.id) << ", " << std::get<2>(node.id) <<
    ", " << node.type <<  ", " << node.pIncrId << ", " << std::get<0>(node.parent_id) << ", " << std::get<1>(node.parent_id) << ", " << 
    std::get<2>(node.parent_id);
    */
    //only need the parents Id for step nodes
    os << node.incrId;
    return os;
}

void TreeNode::serialize(std::ofstream& strm){
    /*
    strm << incrId << ", " << std::get<0>(id) << ", " << std::get<1>(id) << ", " << std::get<2>(id) <<
    ", " << type << ", " << pIncrId << ", " << std::get<0>(parent_id) << ", " << std::get<1>(parent_id) << ", " << 
    std::get<2>(parent_id) << ", "  << pType << ", " << callSiteId << std::endl;
    */
    //add parallel Id to profling log
    //id, type, parallel_id, parentId, callSiteId
    strm << incrId << "," << type << "," << std::get<1>(id) << "," << pIncrId << "," << callSiteId << std::endl;

}

void TreeNode::serializeTask(std::ofstream& strm, uint64_t task_id){
    
    /// id, type, parallel_id, parentId, callSiteId, taskIdT
    strm << incrId << "," << type << "," << std::get<1>(id) << "," << pIncrId << "," << callSiteId << "," << task_id << "T" << std::endl;

}