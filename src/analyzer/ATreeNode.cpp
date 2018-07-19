#include "ATreeNode.h"

ATreeNode::ATreeNode(){
    type = NodeType::NO_TYPE;
    work = 0;
    cWork = 0;
    callSiteId = 0;
    ssList = nullptr;
    regionWork = nullptr;
    lockId = 0;
    criticalSectionWork = 0;
    task_id = 0;
    dep_c_work = 0;
}


ATreeNode::ATreeNode(NodeType node_type){
    type = node_type;
    work = 0;
    cWork = 0;
    callSiteId = 0;
    ssList = nullptr;
    regionWork = nullptr;
    lockId = 0;
    criticalSectionWork = 0;
    task_id = 0;
    dep_c_work = 0;
}

std::ostream& operator<<(std::ostream& os, const ATreeNode& node){
    
    if (node.type == NodeType::STEP){
        os << node.incrId << "," << node.work; 
    }
    else{
        os << node.incrId << "," << node.type <<  ","  << node.pIncrId;
    }
    return os;
}

void ATreeNode::serialize(std::ofstream& strm){
    
}