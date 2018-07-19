#include "profileReader.h"
#include "stringUtil.h"
#include "fileUtil.h"
#include "printtuple.hpp"
#include <algorithm>
#include <stdlib.h>

//set to one to get log report
#define DEBUG 0

ProfileReader::ProfileReader(){
    std::cout << "analyzer looking for log directory" << std::endl;
    
    const char *path = "./log";
	const int exist = dirExists(path);
	if (exist ==1){
		std::cout << "log directory found" << std::endl;
	}
	else{
        std::cout << "analyzer coudn't find the log directroy. exiting" << std::endl;
        exit(1);
    }
    
    std::string file_name = "log/profiler_output.csv";
    outputFile.open(file_name);
    file_name = "log/profiler_log.csv";
    analyzerLog.open(file_name);
    verbose = false;
    fullProfile = false;
    process_task_dep =false;

}

//this needs to be called after buildTree has been called
bool ProfileReader::MayHappenInParallel(ATreeNode* node1, ATreeNode* node2){
    
    
    #if DEBUG
    assert ((node1->pIncrId != 0) && (node2->pIncrId != 0));
    assert (node1->type == NodeType :: STEP);
    assert (node2->type == NodeType :: STEP);
    assert(node1 != node2);
    #endif

    std::vector<ATreeNode*> n1Ancestors;
    std::vector<ATreeNode*> n2Ancestors;
    ATreeNode* n1 = nullptr;
    ATreeNode* n2 = nullptr;
    if ((node1->incrId) < (node2->incrId)){
        n1 = node1;
        n2 = node2;
    }
    else{
        n1 = node2;
        n2 = node1;
    }
    
    while (n1->pIncrId != 0){
        n1Ancestors.push_back(n1);
        n1 = internalNodesMap[n1->pIncrId];
    }
    while (n2->pIncrId != 0){
        n2Ancestors.push_back(n2);
        n2 = internalNodesMap[n2->pIncrId];
    }

    std::reverse(n1Ancestors.begin(), n1Ancestors.end());
    std::reverse(n2Ancestors.begin(), n2Ancestors.end());
    int i =0;
    ATreeNode* lastCommon = nullptr;
    while( (i < n1Ancestors.size()) && (i < n2Ancestors.size()) && (n1Ancestors[i] == n2Ancestors[i]) ){
        lastCommon = n1Ancestors[i];
        i++;
    }

    #if DEBUG
    assert(lastCommon != nullptr);
    #endif

    if (n1Ancestors.size() <= i){//debug
        std::cout << "something is wrong with mayhappenin paralllel i= " << i << ", n1Ancestors = " << n1Ancestors << " , n2Ancestors = " << n2Ancestors << std::endl; 
    }

    #if DEBUG 
    assert(n1Ancestors.size() > i);
    #endif
    //child of LCA
    if (n1Ancestors[i]->type == NodeType::ASYNC){
        return true;
    }
    return false;
}

bool ProfileReader::pIncrIdCompare(ATreeNode* n1, ATreeNode* n2){
     return n1->incrId < n2->incrId;
}

std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t> ProfileReader::ExtractTuple(std::stringstream* sstream){
    ompt_thread_id_t threadid = 0;
    ompt_parallel_id_t parid = 0;
    ompt_task_id_t taskid = 0;
    std::string elemStr; 
    std::getline(*sstream, elemStr, ','); 
    threadid = std::stoll(elemStr);
    std::getline(*sstream, elemStr, ',');
    parid = std::stoll(elemStr);
    std::getline(*sstream, elemStr, ',');
    taskid = std::stoll(elemStr);
    auto res = std::make_tuple(threadid, parid, taskid);
    return res;
}

//todo modify to read the additional element in the log 
void ProfileReader::ReadStepFile(std::string filename){
    std::ifstream stepFile;
    stepFile.open(filename);
    std::cout << filename << std::endl;
    if (stepFile.is_open()){
        std::string line;
        while (std::getline(stepFile, line)){
            ATreeNode* newStep = new ATreeNode(NodeType::STEP);
            //std::cout << line << std::endl;
            std::stringstream *sstream= new std::stringstream(line);
            std::string incrIdStr;
            std::getline(*sstream, incrIdStr, ',');
            newStep->incrId = std::stol(incrIdStr);
            std::string parentIncrIdStr;
            std::getline(*sstream, parentIncrIdStr, ',');
            newStep->pIncrId = std::stol(parentIncrIdStr);
            std::string countStr;
            std::getline(*sstream, countStr, ',');
            auto count = std::stoll(countStr);
            newStep->work = count;
            newStep->cWork = count;
            newStep->eWork = count;
            newStep->callSiteId = 0;
            //check for region data
            std::string region;
            while(std::getline(*sstream, region, ',')) {
	            std::string reg_work;
	            std::getline(*sstream, reg_work, ',');
                
                trim(region);
                trim(reg_work);
                if (region == "L"){//we have a lock and reg_work is the lockId
                    uint64_t lockId = std::stoull(reg_work);
                    //std::cout << "[temp log] found a step using lock id = " << lockId << std::endl;
                    newStep->lockId = lockId;
                    auto lockSet = lockSteps[lockId];
                    if (lockSet == nullptr){
                        auto newLockSet = new std::vector<ATreeNode*>();
                        lockSteps[lockId] = newLockSet;
                    }
                    lockSet = lockSteps[lockId];
                    lockSet->push_back(newStep);
                    
                    continue;
                }
                
                if (newStep->regionWork == nullptr) {
	                newStep->regionWork = new std::map<unsigned long, long>();
	                newStep->regionWork->insert(std::pair<unsigned long, long>(std::stoul(region), std::stol(reg_work)));
	            } 
                else {
	                auto rMap = newStep->regionWork;

	                if (rMap->count(std::stoul(region)) == 0) {
	                    rMap->insert(std::pair<unsigned long, long>(std::stoul(region), std::stol(reg_work)));
	                } 
                    else {
	                    rMap->at(std::stoul(region)) += std::stoul(reg_work);
	                }
	            }
	        }

            delete sstream;
            stepNodes.push_back(newStep);
        }
    }
    stepFile.close();
    
}

void ProfileReader::ReadTreeFile(std::string filename){
    std::ifstream treeFile;
    treeFile.open(filename);
    std::map<unsigned long, NodeType> resolvedMap;
    std::map<unsigned long, ATreeNode*> pendingNodes;
    
    if (treeFile.is_open()){
        std::string line;
        while (std::getline(treeFile, line)){
            std::stringstream *sstream= new std::stringstream(line);
            std::string incrIdStr;
            std::getline(*sstream, incrIdStr, ',');
            ATreeNode* newInternal = new ATreeNode();
            newInternal->incrId = std::stol(incrIdStr);
            std::string typeStr;
            std::getline(*sstream, typeStr, ','); 
            newInternal->type =  static_cast<NodeType>(std::stoi(typeStr));
            #if DEBUG
            assert(newInternal->type != NodeType::STEP);
            #endif
            std::string parIdStr;
            std::getline(*sstream, parIdStr, ',');
            newInternal->parId = std::stol(parIdStr);
            std::string pIncrIdStr;
            std::getline(*sstream, pIncrIdStr, ',');
            newInternal->pIncrId = std::stol(pIncrIdStr);
            std::string callSiteIdStr;
            std::getline(*sstream, callSiteIdStr, ','); 
            newInternal->callSiteId = std::stol(callSiteIdStr);
            std::string taskId;
            while(std::getline(*sstream, taskId, ',')) {
                if (taskId.back() == 'T'){
                    taskId.pop_back();
                }
        
            } 
            if (taskId.empty()){
                newInternal->task_id = 0;
            }
            else{
                newInternal->task_id = std::stoul(taskId);
            }

            delete sstream;
            internalNodes.push_back(newInternal);
        }
    }
    treeFile.close();
    
    

}


//used to read revised node that are used in pbbs tasking
//todo: modify revised node serialization to only record 2 entries. id, callSiteId
void ProfileReader::ReadRevisedTreeFile(std::string filename){
    std::ifstream treeFile;
    treeFile.open(filename);
    
    
    std::cout << "reading revised tree file = " << filename << std::endl;
    if (treeFile.is_open()){
        std::string line;
        while (std::getline(treeFile, line)){
            //std::cout << line << std::endl;
            std::stringstream *sstream= new std::stringstream(line);
            std::string incrIdStr;
            std::getline(*sstream, incrIdStr, ',');
            ATreeNode* newInternal = new ATreeNode();
            newInternal->incrId = std::stol(incrIdStr);
            std::string typeStr;
            std::getline(*sstream, typeStr, ','); 
            newInternal->type =  static_cast<NodeType>(std::stoi(typeStr));
            #if DEBUG
            assert(newInternal->type != NodeType::STEP);
            #endif
            std::string parIdStr;
            std::getline(*sstream, parIdStr, ',');
            newInternal->parId = std::stol(parIdStr);
            std::string pIncrIdStr;
            std::getline(*sstream, pIncrIdStr, ',');
            newInternal->pIncrId = std::stol(pIncrIdStr);  
            std::string callSiteIdStr;
            std::getline(*sstream, callSiteIdStr, ','); 
            newInternal->callSiteId = std::stol(callSiteIdStr);
            delete sstream;
            
            revisedNodesMap.insert(std::pair<unsigned long, ATreeNode*>(newInternal->incrId, newInternal));
        }
    }
    treeFile.close();
    

}

void ProfileReader::ReadTaskDepedenceFile(std::string filename){
    std::ifstream dep_file;
    dep_file.open(filename);
    
    std::cout << "reading dependency file = " << filename << std::endl;

    if (dep_file.is_open()){
        std::string line;
        while (std::getline(dep_file, line)){
            std::stringstream *sstream= new std::stringstream(line);
            std::string task_id_str;
            std::getline(*sstream, task_id_str, ',');
            task_id_t task_id = std::stoul(task_id_str);
            std::string ndeps_str;
            std::getline(*sstream, ndeps_str, ',');
            int ndeps = std::stoi(ndeps_str);
            std::vector<TaskDependence> dependence_v; 
            for (int i=0; i<ndeps; i++){
                struct TaskDependence td;
                std::string addr_str;
                std::getline(*sstream, addr_str, ',');
                std::string dep_type_str;
                std::getline(*sstream, dep_type_str, ',');
                td.addr = std::stoul(addr_str, nullptr, 16);
                td.dep_type = static_cast<DependenceType>(std::stoi(dep_type_str));
                dependence_v.push_back(td);
            }
            task_dependence_map.insert(std::pair<task_id_t, std::vector<TaskDependence>>(task_id, dependence_v));
            process_task_dep = true;
	    delete sstream;
        }
    }

    dep_file.close();    
}

void ProfileReader::ReadStepNodes(std::string filename, int num){
    std::string fileName = "log/" + filename + "_0.csv";
    std::ifstream f(fileName);
    if (!f.good()){
        std::cout << "couldn't find step node information in the log directory. exiting" << std::endl;
        exit(1);
    }
    for (int i=0; i<num; i++){
        std::string fileName = "log/" + filename + "_" + std::to_string(i) + ".csv";
        ReadStepFile(fileName);
    }
    
    
}

void ProfileReader::ReadInternalNodes(std::string filename, int num){
    std::string fileName = "log/" + filename + "_1.csv";
    std::ifstream f(fileName);
    if (!f.good()){
        std::cout << "couldn't find internal node information in the log directory. exiting" << std::endl;
        exit(1);
    }
    
    for (int i=1; i<=num; i++){
        std::string fileName = "log/" + filename + "_" + std::to_string(i) + ".csv";
        ReadTreeFile(fileName);
    }

    //read revised nodes
    for (int i=1; i<=num; i++){
        std::string fileName = "log/rev_" + filename + "_" + std::to_string(i) + ".csv";
        ReadRevisedTreeFile(fileName);
    }
    
        for( auto elem: internalNodes){
            //if there is any nodes in the revised Map, update internalNodes.callSiteId
            if (!revisedNodesMap.empty()){
                if (revisedNodesMap.count(elem->incrId) !=0){
                    auto revisedNode = revisedNodesMap[elem->incrId];
                    elem->callSiteId = revisedNode->callSiteId;
                    //erase map entry
                    revisedNodesMap.erase(elem->incrId);
                    delete revisedNode;
                    //std::cout << "[temp log]: adjusting nodes incrId Node = " << *elem << std::endl;
                }
            }
            #if DEBUG
            assert(internalNodesMap.count(elem->incrId) == 0);
            #endif
            internalNodesMap.insert(std::pair<unsigned long, ATreeNode*>(elem->incrId, elem));
        }
    

    //revised node map should be empty here
    assert(revisedNodesMap.size() == 0);
    
}

void ProfileReader::ReadTaskDepedences(std::string filename, int num){
    std::cout << "Reading task dependences" << std::endl;
    std::string fileName = "log/" + filename + "_1.csv";
    std::ifstream f(fileName);
    if (!f.good()){
        std::cout << "couldn't find task dependency information in the log directory. Will not perform task depedency analysis" << std::endl;
        return;
    }

    for (int i=1; i<=num; i++){
        std::string fileName = "log/" + filename + "_" + std::to_string(i) + ".csv";
        ReadTaskDepedenceFile(fileName);
    }
}

void ProfileReader::ReadCallSites(std::string filename){
    std::ifstream callSiteFile;
    std::string fileName = "log/" + filename + ".csv";
    std::cout << "Reading call sites: " << fileName << std::endl;
    callSiteFile.open(fileName);    
    
    if (callSiteFile.is_open()){
        std::string line;
        while (std::getline(callSiteFile, line)){
            std::size_t commaIndex = line.find_last_of(",");
            #if DEBUG
            assert(commaIndex != std::string::npos);
            #endif
            std::string locStr = line.substr(0,commaIndex);
            //replace all commas in locStr with _ so that csv gets displayed correctly
            std::replace(locStr.begin(), locStr.end(), ',', '_');
            std::string callSiteIdStr = line.substr(commaIndex+1);

            //modify here to read loop info
            if (callSiteIdStr.back() == 'L'){
                callSiteIdStr.pop_back();
                loopCallSites[std::stol(callSiteIdStr)] = true;
            }

            auto callSiteId = std::stol(callSiteIdStr);
            //std::cout << callSiteId << std::endl;
            callSiteIdtoLoc[callSiteId] = locStr;
            //delete sstream;
        }
    }
    //log
    /*for (auto elem: callSiteIdtoLoc){
        std::cout << "callsite id = " << elem.first << ", callsite loc = " << elem.second << std::endl;
    }*/

    callSiteFile.close();
}

void ProfileReader::ReadCausalRegions(std::string filename){
    std::ifstream regionInfoFile;
    std::string fileName = "log/" + filename + ".csv";
    std::cout << "Reading causal regions: " << fileName << std::endl;
    regionInfoFile.open(fileName);    
    
    if (regionInfoFile.is_open()){
        std::string line;
        while (std::getline(regionInfoFile, line)){
            std::stringstream *sstream= new std::stringstream(line);
            std::string regionIdStr;
            std::getline(*sstream, regionIdStr, ',');
            auto regionId = std::stol(regionIdStr);
            
            std::string regionFile;
            std::getline(*sstream, regionFile, ',');

            std::string regionLineStr;
            std::getline(*sstream, regionLineStr, ',');
            auto regionLine = std::stoi(regionLineStr);
            RegionData rData;
            rData.region_filename = regionFile;
            rData.region_line_number = regionLine;
            std::cout << "inserting region id= " << regionId << ", filename= " << regionFile << ", line number= " << regionLine << std::endl; 
            allRegions.insert(std::pair<unsigned long, RegionData>(regionId,rData));
            delete sstream;
        }
    }
    else{
        std::cout << "No region info was found. Will not perform causal profiling." << std::endl;
    }
}

void ProfileReader::BuildTree(){
    unsigned long root = 0;
    dpstTree[root] = new std::vector<ATreeNode*>();
    for(auto elem: internalNodes){
        auto mapId = elem->pIncrId;
        auto childrenVector = dpstTree[mapId];
        if (childrenVector == nullptr){
            auto newChildrenVector = new std::vector<ATreeNode*>();
            dpstTree[mapId] = newChildrenVector;
        }
        childrenVector = dpstTree[mapId];
        childrenVector->push_back(elem);
    }
    for(auto elem: stepNodes){
        auto mapId = elem->pIncrId;//make sure adjustment to read step files are made
        auto childrenVector = dpstTree[mapId];
        if (childrenVector == nullptr){
            auto newChildrenVector = new std::vector<ATreeNode*>();
            dpstTree[mapId] = newChildrenVector;
        }
        childrenVector = dpstTree[mapId];
        childrenVector->push_back(elem);
    
    }
    for(auto elem: dpstTree){
        auto treeId = elem.first;
        auto childrenVector = elem.second;
        struct {
            bool operator()(ATreeNode* a, ATreeNode* b) const
            {   
                return a->incrId < b->incrId;
            }   
        } customLess;
        std::sort(childrenVector->begin(), childrenVector->end(),customLess);
    }
}

void ProfileReader::UpdateSSList(unsigned long callSiteId, long eWork, std::map<unsigned long, long> *ssList){
    if(ssList->count(callSiteId) !=0){
        ssList->at(callSiteId) += eWork;
    }
    else{
        ssList->insert(std::pair<unsigned long,long>(callSiteId, eWork));
    }
}

void ProfileReader::CopySSList(std::map<unsigned long, long> *dst, std::map<unsigned long, long> *src){
    for(auto sElem: *src){
        UpdateSSList(sElem.first, sElem.second, dst);
    }

}

/// TODO: Convert this recursive function to a stack based version
void ProfileReader::ComputeNodeWork(ATreeNode* node){
    //std::cout<< "computeNodeWork node= " << *node << std::endl;
    if(node->type==NodeType::STEP)
        return;
    //assert((node->type==NodeType::ASYNC) || (node->type==NodeType::FINISH));
    //node is not processed yet. just a temporary sanity check
    auto mapNodeId = node->incrId;
    auto nodeChildren = dpstTree[mapNodeId];
    long long stepWork = 0;
    long long finishcWork = 0;
    long long totalWork = 0;
    long long criticalWork = 0;
    long long finisheWork = 0;
    long long exclusiveWork = 0;

    std::map<unsigned long, long> *ssList = new std::map<unsigned long, long>;

    #if DEBUG
    assert(node->ssList == nullptr);
    #endif
    for(auto childNode: *nodeChildren){
        ComputeNodeWork(childNode);
        if (childNode->type == NodeType::ASYNC){
            
            auto leftWork = stepWork + finishcWork;
            
            //task dependency contribution to ciritcal path
            //leftWork+= childNode->dep_c_work;
            if (leftWork + childNode->cWork > criticalWork){
                criticalWork = leftWork + childNode->cWork;
                if (childNode->callSiteId ==0){
                    exclusiveWork =  finisheWork + stepWork + childNode->eWork;
                }
                else{
                    exclusiveWork =  finisheWork + stepWork;
                }
                node->ssList = childNode->ssList;
                CopySSList(node->ssList, ssList);
            }
            else{
                delete childNode->ssList;
            }

        }
        else if (childNode->type == NodeType::FINISH){
            finishcWork+= childNode->cWork;
            if (childNode->callSiteId == 0){
                finisheWork+= childNode->eWork;    
            }
            
            CopySSList(ssList, childNode->ssList);
            delete childNode->ssList;
            if (finishcWork + stepWork > criticalWork){
                node->ssList = ssList; 
                criticalWork = finishcWork + stepWork;
                exclusiveWork = finisheWork + stepWork;
            }

        }
        else if (childNode->type == NodeType::STEP){
            stepWork+= childNode->work;
            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
                exclusiveWork = finisheWork + stepWork;
            }
        }
        totalWork+=childNode->work;

    }
    node->cWork = criticalWork;
    node->work = totalWork;
    node->eWork = exclusiveWork;
    if (node->ssList == nullptr){
        node->ssList = ssList;
    }
    if (node->callSiteId !=0){
        UpdateSSList(node->callSiteId, node->eWork, node->ssList);
    }

    #if DEBUG
    analyzerLog << "node= " << *node << std::endl;
    analyzerLog << "cwork= " << node->cWork << std::endl;
    analyzerLog << "work= " << node->work << std::endl;
    analyzerLog << "ework= " << node->eWork << std::endl;
    #endif
}


/// TODO: Convert this recursive function to a stack based version
void ProfileReader::ComputeNodeWorkTaskDep(ATreeNode* node){
    if(node->type==NodeType::STEP)
        return;
    auto mapNodeId = node->incrId;
    auto nodeChildren = dpstTree[mapNodeId];
    long long stepWork = 0;
    long long finishcWork = 0;
    long long totalWork = 0;
    long long criticalWork = 0;
    long long finisheWork = 0;
    long long exclusiveWork = 0;
    
    std::map<unsigned long, long> *ssList = new std::map<unsigned long, long>;
    
    //task dependence
    std::vector<ATreeNode*> left_async_siblings;
	std::vector<ATreeNode*> all_async_siblings;
    std::vector<int> async_predecessor;
    
    for(auto childNode: *nodeChildren){
        if (childNode->type == NodeType::ASYNC){
            all_async_siblings.push_back(childNode);
            async_predecessor.push_back(-1);
        }
    }
    
    if (node->ssList == nullptr){
    	node->ssList = new std::map<unsigned long, long>;
    }
    else{
    	node->ssList->clear();
    }
    
    
    #if DEBUG
    //assert(node->ssList == nullptr);
    #endif
    /** indexes the async node currenlty being processed from left to right. 
     *  smaller indices have already been processed
     *  larger indices are not processed and should not be processed
     * */
    long async_index = 0;
    for(auto childNode: *nodeChildren){
        ComputeNodeWorkTaskDep(childNode);
        if (childNode->type == NodeType::ASYNC){
            //added task dependency 
            if(HasDependence(childNode)){
		        ProcessNodeDependencies(childNode, all_async_siblings, async_predecessor, async_index);

            }
            auto leftWork = stepWork + finishcWork;
            
            //task dependency contribution to ciritcal path
            leftWork+= childNode->dep_c_work;
            if (leftWork + childNode->cWork > criticalWork){
                criticalWork = leftWork + childNode->cWork;
                if (childNode->callSiteId ==0){
                    exclusiveWork =  finisheWork + stepWork + childNode->eWork;
                }
                else{
                    exclusiveWork =  finisheWork + stepWork;
                }

                //update node ssList
		        node->ssList->clear();
		        CopySSList(node->ssList, childNode->ssList);
                //node->ssList = childNode->ssList;
                CopySSList(node->ssList, ssList);
		        //add ssList of all dependent async nodes on the longest path
                int p_index = async_predecessor[async_index];
                while (p_index !=-1){
                    ATreeNode* dep_sibling = all_async_siblings[p_index];
                    //CopySSList(node->ssList, dep_sibling->ssList);
                    CopySSList(node->ssList, dep_sibling->ssList);
                    p_index = async_predecessor[p_index];
                }
            }
	        async_index++;
            //task dependence 
            //left_async_siblings.push_back(childNode);
        }
        else if (childNode->type == NodeType::FINISH){
            finishcWork+= childNode->cWork;
            if (childNode->callSiteId == 0){
                finisheWork+= childNode->eWork;    
            }
            
            CopySSList(ssList, childNode->ssList);
            delete childNode->ssList;
            if (finishcWork + stepWork > criticalWork){
                node->ssList->clear();
                CopySSList(node->ssList, ssList);
                criticalWork = finishcWork + stepWork;
                exclusiveWork = finisheWork + stepWork;
            }

        }
        else if (childNode->type == NodeType::STEP){
            stepWork+= childNode->work;
            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
                exclusiveWork = finisheWork + stepWork;
            }
        }
        totalWork+=childNode->work;

    }
    node->cWork = criticalWork;
    node->work = totalWork;
    node->eWork = exclusiveWork;
    if (node->ssList == nullptr){//happens if all children are step nodes
        node->ssList = ssList;
    }
    if (node->callSiteId !=0){//check if this works with finish callsites
        UpdateSSList(node->callSiteId, node->eWork, node->ssList);
    }

    #if DEBUG
    analyzerLog << "node= " << *node << std::endl;
    analyzerLog << "cwork= " << node->cWork << std::endl;
    analyzerLog << "work= " << node->work << std::endl;
    analyzerLog << "ework= " << node->eWork << std::endl;
    #endif
}

void ProfileReader::CallSiteDFS(std::vector<ATreeNode*> *ancestors, ATreeNode* node){
    if (node->type == NodeType::STEP)
        return;
    if (node->callSiteId !=0){
        bool found = false;
        for(auto rit = ancestors->rbegin(); rit!= ancestors->rend(); ++rit){
            if ((*rit)->callSiteId == node->callSiteId){
                found = true;
                break;
            }
        }   
        if (!found){
            if (allCallSites.count(node->callSiteId) == 0){
                std::cout << "callsite dfs accessing map element for which key does not exist key= " << node->callSiteId << std::endl;
            }
            
            allCallSites.at(node->callSiteId).work += node->work;
            //check if callsite is a loop
            if (loopCallSites.count(node->callSiteId) == 1){
                std::map<par_id_t, long long>& loopWork = loopInstances[node->callSiteId];
                if (loopWork.count(node->parId) == 0){
                    loopWork.insert(std::pair<par_id_t, long long>(node->parId,0));
                }
                if (loopWork.at(node->parId) < node->cWork){
                    loopWork.at(node->parId) = node->cWork;
                }
            }
            else{
                allCallSites.at(node->callSiteId).cWork += node->cWork;
            }
            
        }
    }

    auto mapNodeId = node->incrId;
    auto nodeChildren = dpstTree[mapNodeId];
    ancestors->push_back(node);
    for(ATreeNode* childNode: *nodeChildren){
        CallSiteDFS(ancestors, childNode);        
    }
    ancestors->pop_back();

}

void ProfileReader::ComputeCallSiteWork(){
    std::vector<ATreeNode*> *ancestors = new std::vector<ATreeNode*>();
    int height=0;
    unsigned long root = 0;
    auto rootChildren = dpstTree[root];
    for(auto elem: callSiteIdtoLoc){
        #if DEBUG
        assert(allCallSites.count(elem.first) == 0);
        #endif
        CallSiteWork csWork;
        csWork.work = 0;
        csWork.cWork = 0;
        csWork.height = 42;
        
        allCallSites.insert(std::pair<unsigned long, CallSiteWork>(elem.first, csWork));
    }
    ATreeNode* treeNode = rootChildren->at(0);
    #if DEBUG
    assert (treeNode!=nullptr);
    #endif
    CallSiteDFS(ancestors, treeNode);
  
    for(auto elem: loopInstances){
        unsigned long loopCallSite = elem.first;
        long long loopCWork= 0;
        for (auto loopElem: elem.second){
            loopCWork+=loopElem.second;
        }
        allCallSites[loopCallSite].cWork = loopCWork;
        //std::unordered_map<unsigned long, CallSiteWork> allCallSites;

    }
}

//This needs to be called after build tree
void ProfileReader::ComputeAllWork(int mode){
    unsigned long root = 0;
    assert(dpstTree[root]!=nullptr);
    auto rootChildren = dpstTree[root];
    assert(rootChildren->size()==1);
    std::cout << "root has " << rootChildren->size() << " children" << std::endl;
    auto childrenVector = dpstTree[root];
    long long work = 0;
    long long cWork = 0;
    long long eWork = 0;
    //make this to set root's members
    if(process_task_dep == false){
	for(auto childNode: *childrenVector){
        	ComputeNodeWork(childNode);
        	work+= childNode->work;
        	cWork+= childNode->cWork; 
      	  	eWork+= childNode->eWork;
    	}
    }
    else{
    	for(auto childNode: *childrenVector){
        	ComputeNodeWorkTaskDep(childNode);
        	work+= childNode->work;
        	cWork+= childNode->cWork; 
      	  	eWork+= childNode->eWork;
    	}
    }
    
    programTotalWork = work;
    ComputeCallSiteWork();
    
    if (verbose == true){
        outputFile << "code region, work, critical work, parallelism, exclusive/criticalpath work, critical path percentage" << std::endl;
        outputFile << "main, " << work << ", " << cWork << ", " << static_cast<double>(work)/cWork 
        <<  ", " << eWork << ", " << (static_cast<double>(eWork)/cWork) * 100 << std::endl;
    } 
    else{
        outputFile << "code region, parallelism , critical path percentage" << std::endl;
        outputFile << "main, " << static_cast<double>(work)/cWork 
        << ", " << (static_cast<double>(eWork)/cWork) * 100 << std::endl;
    }
    
    std::cout << "-----------------------" << std::endl;
    std::cout << "callSite count = " << allCallSites.size() << std::endl;
    for(auto elem:allCallSites){
        std::cout << "callSite = " << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3) << std::endl;
        std::cout << "work = " << elem.second.work << std::endl;
        std::cout << "critical work = " << elem.second.cWork << std::endl;
        std::cout << "parallelism = " << static_cast<double>(elem.second.work)/elem.second.cWork << std::endl;    
        //std::cout << "height = " << elem.second.height<< std::endl;
        if (verbose == true){
            if (callSiteIdtoLoc[elem.first].back() == ';'){
                outputFile << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
            }
            else{
                outputFile << callSiteIdtoLoc[elem.first]; 
            }
            
            outputFile << ", " << elem.second.work << ", " << elem.second.cWork
            << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
            auto ssList = rootChildren->at(0)->ssList;    
            if (ssList->count(elem.first)==0){
                outputFile << ", 0, 0" << std::endl;
            }
            else{
                auto cPathWork = ssList->at(elem.first);
                outputFile << ", " << cPathWork << ", " << (static_cast<double>(cPathWork)/cWork)*100 << std::endl;
            }
        }
        else{
            if (callSiteIdtoLoc[elem.first].back() == ';'){
                outputFile << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
            }
            else{
                outputFile << callSiteIdtoLoc[elem.first]; 
            }
        
            outputFile << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
            auto ssList = rootChildren->at(0)->ssList;    
            if (ssList->count(elem.first)==0){
                outputFile << ", 0" << std::endl;
            }
            else{
                auto cPathWork = ssList->at(elem.first);
                outputFile << ", " << (static_cast<double>(cPathWork)/cWork)*100 << std::endl;
            }
        }
        
    }
    

    double allSitesCPP = 0;
    auto ssList = rootChildren->at(0)->ssList;
    std::cout << "-----------------------" << std::endl;
    std::cout << "ssList count" << ssList->size() << std::endl;
    for(auto elem:*ssList){
        std::cout << "callSite = " << callSiteIdtoLoc[elem.first] << std::endl;
        std::cout << "work on critical path = " << elem.second << std::endl;
        std::cout << "critcal path percentage = " << static_cast<double>(elem.second)/cWork << std::endl;
        double temp = static_cast<double>(elem.second)/cWork;
        allSitesCPP += temp;
    }
    //delete root ssList
    delete ssList;
    std::cout << "analysis done!" << std::endl;
    std::cout << "program work = " << work << std::endl;
    std::cout << "critical work = " << cWork << std::endl;
    std::cout << "program exclusive work = " << eWork << std::endl;
    std::cout << "main critical path percentage = " << static_cast<double>(eWork)/cWork << std::endl;
    std::cout << "parallelism = " << static_cast<double>(work)/cWork << std::endl;
    double temp = static_cast<double>(eWork)/cWork;
    allSitesCPP += temp;
    std::cout << "w sum of all critical paths = " << allSitesCPP << std::endl;
    //outputFile << "analysis done!" << std::endl;
    
    //add condition to choose between the two
    if (fullProfile){
        FullCausalProfile();
    }
    else{
        CausalProfile();
    }
    
    

    //add condition to not perform lock profile if there is no lock. check lockSteps size
    if (mode==1){
        std::cout << "performing lock analysis" << std::endl;
        LockProfile();
    }
    
    
}

void ProfileReader::CausalProfile(){


    int rId = 0;
    for(auto regionElem: allRegions){
        std::ofstream report;
        rId++;
        std::string filename = "log/region_prof" + std::to_string(rId) + ".csv";
        report.open(filename);
        auto regionInfo = regionElem.second;
        report << regionInfo.region_filename << "," << regionInfo.region_line_number << std::endl; 
        report << "Region Optimization factor, Whole program parallelism, Work, CWork" << std::endl;

        int increaseFactor = 1;
        for(int i=0; i< PAR_INC_COUNT; i++){
            int increaseAmount = PAR_INC_INIT;
            increaseAmount *= increaseFactor;

            unsigned long root = 0;
            #if DEBUG
            assert(dpstTree[root]!=nullptr);
            #endif
            auto rootChildren = dpstTree[root];
            auto rootChild = rootChildren->at(0);
            ComputeNodeWorkCausal(rootChild, regionElem.first, increaseAmount);
            long long work = rootChild->work;
            long long cWork = rootChild->cWork;
            double par = static_cast<double>(work)/cWork;
            report << increaseAmount << ", " << par << ", " << work << ", " << cWork << std::endl;
            increaseFactor *= PAR_INC_FACTOR;
        }
        report.close();
    
    }

    if (!allRegions.empty()){
        std::ofstream report;
        std::string filename = "log/all_region_prof.csv";
        report.open(filename);
        report << "Region Optimization factor, Whole program parallelism, Work, CWork" << std::endl;

        int increaseFactor = 1;
        for(int i=0; i< PAR_INC_COUNT; i++){
            int increaseAmount = PAR_INC_INIT;
            increaseAmount *= increaseFactor;

            unsigned long root = 0;
            #if DEBUG
            assert(dpstTree[root]!=nullptr);
            #endif
            auto rootChildren = dpstTree[root];
            auto rootChild = rootChildren->at(0);
            ComputeNodeWorkCausal(rootChild, 0, increaseAmount);
            long long work = rootChild->work;
            long long cWork = rootChild->cWork;
            double par = static_cast<double>(work)/cWork;
            report << increaseAmount << ", " << par << ", " << work << ", " << cWork << std::endl;
            increaseFactor *= PAR_INC_FACTOR;
        }
        report.close();

    }
}

void ProfileReader::ComputeCallSiteWorkCausal(){
    std::vector<ATreeNode*> *ancestors = new std::vector<ATreeNode*>();
    unsigned long root = 0;
    auto rootChildren = dpstTree[root];
    for(auto& elem: allCallSites){//reset callsite work
        auto csWork = elem.second;
        elem.second.work = 0;
        elem.second.cWork = 0;
        elem.second.height = 42;
    }
    

    ATreeNode* treeNode = rootChildren->at(0);
    #if DEBUG
    assert (treeNode!=nullptr);
    #endif
    CallSiteDFS(ancestors, treeNode);
    for(auto elem: loopInstances){
        unsigned long loopCallSite = elem.first;
        long long loopCWork= 0;
        for (auto loopElem: elem.second){
            loopCWork+=loopElem.second;
        }
        allCallSites[loopCallSite].cWork = loopCWork;
    }
}

void ProfileReader::FullCausalProfile(){
    int rId = 0;
    for(auto regionElem: allRegions){
        std::ofstream report;
        rId++;
        std::string filename = "log/region_prof" + std::to_string(rId) + ".csv";
        report.open(filename);
        auto regionInfo = regionElem.second;
        report << regionInfo.region_filename << "," << regionInfo.region_line_number << std::endl; 
        
        int increaseFactor = 1;


        for(int i=0; i< PAR_INC_COUNT; i++){
            int increaseAmount = PAR_INC_INIT;
            increaseAmount *= increaseFactor;

            unsigned long root = 0;
            #if DEBUG
            assert(dpstTree[root]!=nullptr);
            #endif
            auto rootChildren = dpstTree[root];
            auto rootChild = rootChildren->at(0);
            loopInstances.clear();
            FullComputeNodeWorkCausal(rootChild, regionElem.first, increaseAmount);
            ComputeCallSiteWorkCausal();
            long long work = rootChild->work;
            long long cWork = rootChild->cWork;
            long long eWork = rootChild->eWork;
            double par = static_cast<double>(work)/cWork;
            report << "Region Optimization factor, Whole program parallelism, Work, CWork" << std::endl;
            report << increaseAmount << ", " << par << ", " << work << ", " << cWork << std::endl;
            //write callsite profile
            if (verbose == true){
                report << "code region, work, critical work, parallelism, exclusive/criticalpath work, critical path percentage" << std::endl;
                report << "main, " << work << ", " << cWork << ", " << static_cast<double>(work)/cWork 
                <<  ", " << eWork << ", " << (static_cast<double>(eWork)/cWork) * 100 << std::endl;
            } 
            else{
                report << "code region, parallelism , critical path percentage" << std::endl;
                report << "main, " << static_cast<double>(work)/cWork 
                << ", " << (static_cast<double>(eWork)/cWork) * 100 << std::endl;
            }
            
            for(auto elem:allCallSites){
                if (verbose == true){
                    if (callSiteIdtoLoc[elem.first].back() == ';'){
                        report << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
                    }
                    else{
                        report << callSiteIdtoLoc[elem.first]; 
                    }

                    report << ", " << elem.second.work << ", " << elem.second.cWork
                    << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
                    auto ssList = rootChildren->at(0)->ssList;    
                    if (ssList->count(elem.first)==0){
                        report << ", 0, 0" << std::endl;
                    }
                    else{
                        auto cPathWork = ssList->at(elem.first);
                        report << ", " << cPathWork << ", " << (static_cast<double>(cPathWork)/cWork)*100 << std::endl;
                    }
                }
                else{
                    if (callSiteIdtoLoc[elem.first].back() == ';'){
                        report << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
                    }
                    else{
                        report << callSiteIdtoLoc[elem.first]; 
                    }

                    report << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
                    auto ssList = rootChildren->at(0)->ssList;    
                    if (ssList->count(elem.first)==0){
                        report << ", 0" << std::endl;
                    }
                    else{
                        auto cPathWork = ssList->at(elem.first);
                        report << ", " << (static_cast<double>(cPathWork)/cWork)*100 << std::endl;
                    }
                }

            }

            increaseFactor *= PAR_INC_FACTOR;

            delete rootChild->ssList;
        }
        report.close();
    
    }

    if (!allRegions.empty()){
        std::ofstream report;
        std::string filename = "log/all_region_prof.csv";
        report.open(filename);
        
        int increaseFactor = 1;
        for(int i=0; i< PAR_INC_COUNT; i++){
            int increaseAmount = PAR_INC_INIT;
            increaseAmount *= increaseFactor;

            unsigned long root = 0;
            #if DEBUG
            assert(dpstTree[root]!=nullptr);
            #endif
            auto rootChildren = dpstTree[root];
            auto rootChild = rootChildren->at(0);
            loopInstances.clear();
            FullComputeNodeWorkCausal(rootChild, 0, increaseAmount);
            ComputeCallSiteWorkCausal();
            long long work = rootChild->work;
            long long cWork = rootChild->cWork;
            long long eWork = rootChild->eWork;
            double par = static_cast<double>(work)/cWork;
            report << "Region Optimization factor, Whole program parallelism, Work, CWork" << std::endl;
            report << increaseAmount << ", " << par << ", " << work << ", " << cWork << std::endl;
            //write callsite profile
            if (verbose == true){
                report << "code region, work, critical work, parallelism, exclusive/criticalpath work, critical path percentage" << std::endl;
                report << "main, " << work << ", " << cWork << ", " << static_cast<double>(work)/cWork 
                <<  ", " << eWork << ", " << (static_cast<double>(eWork)/cWork) * 100 << std::endl;
            } 
            else{
                report << "code region, parallelism , critical path percentage" << std::endl;
                report << "main, " << static_cast<double>(work)/cWork 
                << ", " << (static_cast<double>(eWork)/cWork) * 100 << std::endl;
            }
            for(auto elem:allCallSites){
                if (verbose == true){
                    if (callSiteIdtoLoc[elem.first].back() == ';'){
                        report << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
                    }
                    else{
                        report << callSiteIdtoLoc[elem.first]; 
                    }

                    report << ", " << elem.second.work << ", " << elem.second.cWork
                    << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
                    auto ssList = rootChildren->at(0)->ssList;    
                    if (ssList->count(elem.first)==0){
                        report << ", 0, 0" << std::endl;
                    }
                    else{
                        auto cPathWork = ssList->at(elem.first);
                        report << ", " << cPathWork << ", " << (static_cast<double>(cPathWork)/cWork)*100 << std::endl;
                    }
                }
                else{
                    if (callSiteIdtoLoc[elem.first].back() == ';'){
                        report << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
                    }
                    else{
                        report << callSiteIdtoLoc[elem.first]; 
                    }

                    report << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
                    auto ssList = rootChildren->at(0)->ssList;    
                    if (ssList->count(elem.first)==0){
                        report << ", 0" << std::endl;
                    }
                    else{
                        auto cPathWork = ssList->at(elem.first);
                        report << ", " << (static_cast<double>(cPathWork)/cWork)*100 << std::endl;
                    }
                }

            }

            increaseFactor *= PAR_INC_FACTOR;
            delete rootChild->ssList;
        }
        report.close();

    }
}

void ProfileReader::FullComputeNodeWorkCausal(ATreeNode* node, unsigned long regionId, int speedup){
    if(node->type==NodeType::STEP)
        return;
    #if DEBUG
    assert((node->type==NodeType::ASYNC) || (node->type==NodeType::FINISH));
    #endif
    auto mapNodeId = node->incrId;
    auto nodeChildren = dpstTree[mapNodeId];
    long long stepWork = 0;
    long long finishcWork = 0;
    long long totalWork = 0;
    long long criticalWork = 0;
    long long finisheWork = 0;
    long long exclusiveWork = 0;
    std::map<unsigned long, long> *ssList = new std::map<unsigned long, long>;
    //cleanup the original ssList
    node->ssList = nullptr;

    for(auto childNode: *nodeChildren){
        FullComputeNodeWorkCausal(childNode, regionId, speedup);
        if (childNode->type == NodeType::ASYNC){
            auto leftWork = stepWork + finishcWork;

            if (leftWork + childNode->cWork > criticalWork){
                criticalWork = leftWork + childNode->cWork;
                if (childNode->callSiteId ==0){
                    exclusiveWork =  finisheWork + stepWork + childNode->eWork;
                }
                else{
                    exclusiveWork =  finisheWork + stepWork;
                }

                //update node ssList
                node->ssList = childNode->ssList;
                CopySSList(node->ssList, ssList);
            }
            else{
                delete childNode->ssList;
            }

        }
        else if (childNode->type == NodeType::FINISH){
            finishcWork+= childNode->cWork;
            if (childNode->callSiteId == 0){
                finisheWork+= childNode->eWork;    
            }
            
            CopySSList(ssList, childNode->ssList);
            delete childNode->ssList;
            if (finishcWork + stepWork > criticalWork){
                node->ssList = ssList; 
                criticalWork = finishcWork + stepWork;
                exclusiveWork = finisheWork + stepWork;
            }
        }
        else if (childNode->type == NodeType::STEP){
            std::map<unsigned long, long> *nodeRegionWork;
            nodeRegionWork = childNode->regionWork;
            long long regionWorkDec = 0;
            if (nodeRegionWork!=nullptr){
                

                if(regionId==0){
                    for(auto regionElem: *nodeRegionWork){
                        long long elemDec = regionElem.second;
                        elemDec = elemDec - (elemDec/speedup);
                        regionWorkDec += elemDec;    
                    }
                } 
                else if (nodeRegionWork->count(regionId) != 0){
                    long long elemDec = nodeRegionWork->at(regionId);
                    elemDec = elemDec - (elemDec/speedup);
                    regionWorkDec += elemDec;
                }
            }

            
            stepWork+= childNode->work;
            
            stepWork-= regionWorkDec;
            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
                exclusiveWork = finisheWork + stepWork;
            }
        }
        totalWork+=childNode->work;
    }
    node->cWork = criticalWork;
    node->work = totalWork;
    node->eWork = exclusiveWork;
    if (node->ssList == nullptr){//happens if all children are step nodes
        node->ssList = ssList;
    }
    if (node->callSiteId !=0){//check if this works with finish callsites
        UpdateSSList(node->callSiteId, node->eWork, node->ssList);
    }

    #if DEBUG
    analyzerLog << "causal node= " << *node << std::endl;
    analyzerLog << "causal cwork= " << node->cWork << std::endl;
    analyzerLog << "causal work= " << node->work << std::endl;
    analyzerLog << "causal ework= " << node->eWork << std::endl;
    #endif
}

void ProfileReader::ComputeNodeWorkCausal(ATreeNode* node, unsigned long regionId, int speedup){
    if(node->type==NodeType::STEP)
        return;
    #if DEBUG
    assert((node->type==NodeType::ASYNC) || (node->type==NodeType::FINISH));
    #endif
    auto mapNodeId = node->incrId;
    auto nodeChildren = dpstTree[mapNodeId];
    long long stepWork = 0;
    long long finishcWork = 0;
    long long totalWork = 0;
    long long criticalWork = 0;


    for(auto childNode: *nodeChildren){
        ComputeNodeWorkCausal(childNode, regionId, speedup);
        if (childNode->type == NodeType::ASYNC){
            auto leftWork = stepWork + finishcWork;
            if (leftWork + childNode->cWork > criticalWork){
                criticalWork = leftWork + childNode->cWork;
            }

        }
        else if (childNode->type == NodeType::FINISH){
            finishcWork+= childNode->cWork;
            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
            }

        }
        else if (childNode->type == NodeType::STEP){
            std::map<unsigned long, long> *nodeRegionWork;
            nodeRegionWork = childNode->regionWork;
            long long regionWorkDec = 0;
            if (nodeRegionWork!=nullptr){
                

                if(regionId==0){
                    for(auto regionElem: *nodeRegionWork){
                        long long elemDec = regionElem.second;
                        elemDec = elemDec - (elemDec/speedup);
                        regionWorkDec += elemDec;    
                    }
                } 
                else if (nodeRegionWork->count(regionId) != 0){
                    long long elemDec = nodeRegionWork->at(regionId);
                    elemDec = elemDec - (elemDec/speedup);
                    regionWorkDec += elemDec;
                }
            }

            
            stepWork+= childNode->work;
            
            stepWork-= regionWorkDec;
            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
            }
        }

        totalWork+=childNode->work;
    }
    node->cWork = criticalWork;
    node->work = totalWork;

}

//Todo
void ProfileReader::LockProfile(){
    
    struct {
        bool operator()(ATreeNode* a, ATreeNode* b) const
        {   
            return a->incrId < b->incrId;
        }   
    } customLess;
    
    for(auto elem: lockSteps){
        auto lockId = elem.first;
        std::vector<ATreeNode*>* steps = elem.second;
        std::sort(steps->begin(), steps->end(), customLess);
    }
    

    for(auto elem: lockSteps){
        auto lockId = elem.first;
        std::vector<ATreeNode*>* steps = elem.second;
        std::vector<ATreeNode*>& stepsRef = *steps;
        
        
        std::cout << "lock analysis for lock id = " << lockId << ", num of steps = " << steps->size() << std::endl;
        std::vector<bool> useFlag(steps->size(), false);
        for(int i=1; i< steps->size(); i++){
            long cumulativeWait = 0;

            //if (i%10000 == 0)
                //std::cout << "[temp log] lock analysis i = " << i << std::endl;
            //this assumes we have infinite worker count otherwise
            //j=i+1-workerCount
            for(int j=0; j<i; j++){
                //std::cout << "checking may happen parallel step id= " << stepsRef[i]->incrId <<  ", step id= " << stepsRef[j]->incrId << std::endl;
                if((!useFlag[i]) && (MayHappenInParallel(stepsRef[i], stepsRef[j]))){
                    //std::cout << "may happen in parallel step id= " << stepsRef[i]->incrId <<  ", step id= " << stepsRef[j]->incrId << std::endl;
                    useFlag[i] = true;
                    cumulativeWait+=stepsRef[j]->work;
                }
            }
            //std::cout << "adjusting work of step id = " << stepsRef[i]->incrId << " to value = " << cumulativeWait << std::endl;
            stepsRef[i]->criticalSectionWork = cumulativeWait;
            
        }

        
    }

    std::cout << "computing critical path using adjusted values" << std::endl;

    unsigned long root = 0;
    #if DEBUG
    assert(dpstTree[root]!=nullptr);
    #endif
    auto rootChildren = dpstTree[root];
    auto rootChild = rootChildren->at(0);
    std::cout << "[temp log] calling computeNodeLock root" << std::endl;
    ComputeNodeWorkLock(rootChild);
    long long work = rootChild->work;
    long long cWork = rootChild->cWork;
    double parIncreaseWork = static_cast<double>(work)/cWork;
    double par = static_cast<double>(programTotalWork)/cWork;
    std::cout << "lock analysis done!" << std::endl;
    std::cout << "program work(lockAdjusted), program work(orig), program criticalwork(lockAdjusted), program parallelism(increase work), program parallelism" << std::endl;
    std::cout << work << ", " << programTotalWork << ", " << cWork << ", " << parIncreaseWork << ", " << par << std::endl;
}

void ProfileReader::ComputeNodeWorkLock(ATreeNode* node){
    if(node->type==NodeType::STEP)
        return;
    #if DEBUG
    assert((node->type==NodeType::ASYNC) || (node->type==NodeType::FINISH));
    #endif
    auto mapNodeId = node->incrId;
    auto nodeChildren = dpstTree[mapNodeId];
    long long stepWork = 0;
    long long finishcWork = 0;
    long long totalWork = 0;
    long long criticalWork = 0;

    
    for(auto childNode: *nodeChildren){
        ComputeNodeWorkLock(childNode);
        if (childNode->type == NodeType::ASYNC){
            auto leftWork = stepWork + finishcWork;
            if (leftWork + childNode->cWork > criticalWork){
                criticalWork = leftWork + childNode->cWork;
            }

        }
        else if (childNode->type == NodeType::FINISH){
            finishcWork+= childNode->cWork;
            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
            }

        }
        else if (childNode->type == NodeType::STEP){
            stepWork+= childNode->work;
            stepWork+= childNode->criticalSectionWork;

            if (finishcWork + stepWork > criticalWork){
                criticalWork = finishcWork + stepWork;
            }
            
            totalWork+=childNode->criticalSectionWork;
        }
        totalWork+=childNode->work;
    }
    node->cWork = criticalWork;
    node->work = totalWork;

}

/// Given a tree node, checks whether it has any task dependencies
bool ProfileReader::HasDependence(ATreeNode* node){
    if (task_dependence_map.count(node->task_id) == 1){
        return true;
    }
    return false;
}
/// is node dependent on leftsibling
bool ProfileReader::AreDependent(ATreeNode* node, ATreeNode* leftsibling){
    const std::vector<TaskDependence>& n_deps = task_dependence_map.at(node->task_id);
    const std::vector<TaskDependence>& s_deps = task_dependence_map.at(leftsibling->task_id);
    for(auto n_dep: n_deps){
        for(auto s_dep: s_deps){
            // first check that they operate on the same addr
            if(n_dep.addr == s_dep.addr){
                assert(n_dep.dep_type != DependenceType::NO_TYPE);
                assert(s_dep.dep_type != DependenceType::NO_TYPE);
                if((n_dep.dep_type == DependenceType::IN_OUT) || (n_dep.dep_type == DependenceType::OUT)){
                    //regardless of s' dependency type we have a dependency
                    return true;
                }
                else if(n_dep.dep_type == DependenceType::IN){
                    if((s_dep.dep_type == DependenceType::IN_OUT) || (s_dep.dep_type == DependenceType::OUT)){
                        return true;
                    }
                }
                else{
                    std::cout << "[in function HasDependence] unexpected dependency type. exiting" << std::endl;
                    exit(1);
                }
            }
        }
    }
    return false;
}
/// Find all left sibling tasks that node is a dependent upon and update node.dep_c_work 
void ProfileReader::ProcessNodeDependencies(ATreeNode* node, 
                                            std::vector<ATreeNode*> all_async_siblings, 
                                            std::vector<int> &async_predecessor,
                                            int last_index)
{
    node->dep_c_work = 0;
    for(int i=0; i<last_index; i++){
        ATreeNode* left_sibling = all_async_siblings[i]; 
        if(AreDependent(node, left_sibling)){
            if( (left_sibling->dep_c_work + left_sibling->cWork) > node->dep_c_work ){
                node->dep_c_work = left_sibling->dep_c_work + left_sibling->cWork;
                async_predecessor[last_index] = i;
            }
        }
    }    
}

void ProfileReader::PrintStepNodes(){
    std::cout << "no of step nodes= " << stepNodes.size() << std::endl;
    long long totalWork = 0;
    for (auto elem: stepNodes){
        std::cout << *elem << std::endl;
        totalWork+=elem->work;
    }
    std::cout << "Total work= " << totalWork << std::endl;
}

void ProfileReader::PrintInternalNodes(){
    std::cout << "no of internal nodes= " << internalNodes.size() << std::endl;
    for (auto elem: internalNodes){
        std::cout << *elem << std::endl;
    }
}

void ProfileReader::Cleanup(){
    for(auto elem: stepNodes){
        delete elem;
    }
    for(auto elem: internalNodes){
        delete elem;
    }
    //the flush is redundant
    analyzerLog.flush();
    analyzerLog.close();
    outputFile.flush();
    outputFile.close();
}