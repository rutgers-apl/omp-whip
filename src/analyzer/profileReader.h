#ifndef POFILEREADER_H_
#define POFILEREADER_H_

#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <utility>

#include "AGlobals.h"
#include "ATreeNode.h"
#include "CallSiteWork.h"
#include "TaskDependency.h"

#define PAR_INC_COUNT 2
#define PAR_INC_INIT 16
#define PAR_INC_FACTOR 4

typedef std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t, NodeType> ompID_t; 
typedef std::pair<unsigned long, unsigned long> callsite_parid_pair_t;
typedef unsigned long callsite_id_t;
typedef unsigned long par_id_t;

class ProfileReader{
private:
    std::vector<ATreeNode*> stepNodes;
    std::vector<ATreeNode*> internalNodes;
    std::unordered_map<unsigned long, ATreeNode*> internalNodesMap;
    //revise
    std::unordered_map<unsigned long, ATreeNode*> revisedNodesMap;
    std::unordered_map<unsigned long, std::vector<ATreeNode*>*> dpstTree;
    std::unordered_map<unsigned long, std::string> callSiteIdtoLoc;
    std::unordered_map<unsigned long, CallSiteWork> allCallSites;
    std::unordered_map<unsigned long, RegionData> allRegions;
    std::map<task_id_t, std::vector<TaskDependence>> task_dependence_map;
    std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t> ExtractTuple(std::stringstream* sstream);
    std::ofstream outputFile;
    std::ofstream analyzerLog;
    void ReadStepFile(std::string filename);
    void ReadTreeFile(std::string filename);
    //revisedNodes
    void ReadRevisedTreeFile(std::string filename);
    void ComputeNodeWork(ATreeNode* node);
    void ComputeNodeWorkTaskDep(ATreeNode* node);
    void CopySSList(std::map<unsigned long, long> *dst, std::map<unsigned long, long> *src);
    void UpdateSSList(unsigned long callSiteId, long eWork, std::map<unsigned long, long> *ssList);
    void CallSiteDFS(std::vector<ATreeNode*> *ancestors, ATreeNode* node);
    void ComputeCallSiteWork();
    void CausalProfile();
    void ComputeNodeWorkCausal(ATreeNode* node, unsigned long regionId, int speedup);
    //support for callsite analysis in causal profiling
    void FullCausalProfile();
    void FullComputeNodeWorkCausal(ATreeNode* node, unsigned long regionId, int speedup);
    void ComputeCallSiteWorkCausal();
    //lock and lock util
    std::map<uint64_t, std::vector<ATreeNode*>*> lockSteps;
    bool pIncrIdCompare(ATreeNode* n1, ATreeNode* n2);
    bool MayHappenInParallel(ATreeNode* node1, ATreeNode* node2);
    void LockProfile();
    void ComputeNodeWorkLock(ATreeNode* node);
    long long programTotalWork;
    //loop callsites
    std::unordered_map<unsigned long, bool> loopCallSites;
    std::map<callsite_id_t, std::map<par_id_t, long long>> loopInstances;
    //task dependencies
    /// Read a task dependency log files and fill the task_dependence_map
    void ReadTaskDepedenceFile(std::string filename);
    /// Given a tree node, checks whether it has any task dependencies
    bool HasDependence(ATreeNode* node);
    /// Is node dependent on leftsibling
    bool AreDependent(ATreeNode* node, ATreeNode* leftsibling);
    /// Find all left sibling tasks that node is a dependent upon and update node.dep_c_work 
    //void ProcessNodeDependencies(ATreeNode* node, std::vector<ATreeNode*> leftsiblings);
    void ProcessNodeDependencies(ATreeNode* node, 
                                 std::vector<ATreeNode*> all_async_siblings, 
                                 std::vector<int> &async_predecessor,
                                 int last_index);
public:
    //options
    bool process_task_dep;
    bool verbose;
    //fullProfileFlag = 1 enable full causal profiling
    bool fullProfile;
    ProfileReader(); 
    void ReadStepNodes(std::string filename, int num);
    void ReadInternalNodes(std::string filename, int num);
    
    void ReadCallSites(std::string filename);
    void ReadCausalRegions(std::string filename);
    
    /// Read task dependency log files and fill the task_dependence_map
    void ReadTaskDepedences(std::string filename, int num);
    /// Build the dpst like tree datastructure by filling dpstTree
    void BuildTree();

    void ComputeAllWork(int mode);
    void PrintStepNodes();
    void PrintInternalNodes();
    void Cleanup();
    
};


#endif
