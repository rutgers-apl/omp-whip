#ifndef OPENMP_PROFILER_H_
#define OPENMP_PROFILER_H_

#include "globals.h"
#include <ompt-regression.h>
#include "treeNode.h"
#include <sstream>
#include <algorithm>
#include <unordered_map>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "CallSiteWork.h"

#define NUM_ENTRIES 1024

typedef std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t> node_id_t; 
extern std::atomic<unsigned long> node_ctr;
extern std::atomic<unsigned long> callSite_ctr;



class OpenmpProfiler{
private:
	int ompp_initialized;
	std::ofstream report[NUM_THREADS];
	/// Log task dependencies
	//std::ofstream task_dependencies[NUM_THREADS+1];
	std::ofstream callsite_info;

	std::vector<TreeNode*> last_nodes[NUM_THREADS+1];
	std::map<ompt_task_id_t, TreeNode*> taskMap;
	std::map<ompt_task_id_t, std::string> shadowTaskMap;
	std::map<node_id_t, unsigned long> pendingTaskNode;
	std::map<std::string, unsigned long> callSiteMap;

	std::unordered_map<unsigned long, CallSiteWork> allCallSites;
	//for loop analysis
	std::map<unsigned long, bool> loopCallSites;
	std::map<unsigned long, std::map<unsigned long, long long>> loopInstances;
	//index to the finish node for current parallel region
	unsigned int num_threads;
	unsigned int  parNodeIndex;
	//flag to avoid race in barrierBegin/end
	int parBeginFlag;
	int parEndFlag;
	int barBeginFlag;
	int barEndFlag;
	int barFirstThread;
	int implEndCount;
	//new sync solution
	unsigned long barrierCount[NUM_THREADS+1];
	std::vector<TreeNode*> pendingFinish;
	//ompp_loc
	//std::string latestCallSite[NUM_THREADS+1];
	std::map<std::string, unsigned long> callSitesToRemove;
	//int prevParRegion;
	//loop dynamic
	unsigned long prevLoopCallsite[NUM_THREADS+1];
	int firstDispatch[NUM_THREADS+1];//1 denotes its the first dispatch, 0 otherwise
	
	void cleanupFinishNode(ompt_thread_id_t tid);
	void cleanupNode(TreeNode* node);
	//util
	int createDir(const char* dirName);
	int dirExists(const char *path);
	//for online profiling
	bool verbose{true};
	void pushRootNode(ompt_thread_id_t tid, TreeNode* node);
	void pushNode(ompt_thread_id_t tid, TreeNode* node, bool is_barrier_finish);
	TreeNode* popNode(ompt_thread_id_t tid);
	TreeNode* getParent(ompt_thread_id_t tid);
	void CopySSList(std::map<unsigned long, long> *dst, std::map<unsigned long, long> *src);
    void UpdateSSList(unsigned long callSiteId, long eWork, std::map<unsigned long, long> *ssList);
	void GenerateProfile();
public:
	OpenmpProfiler();
	void initProfiler(ompt_thread_id_t serial_thread_id, ompt_parallel_id_t serial_parallel_id, ompt_task_id_t serial_task_id);
	void captureThreadBegin(ompt_thread_id_t tid);
	void captureThreadEnd(ompt_thread_id_t tid);
	void captureParallelBegin(ompt_thread_id_t tid,ompt_task_id_t parent_task_id, ompt_parallel_id_t parallel_id, uint32_t requested_team_size, void *parallel_function, const char* loc);
	void captureParallelEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureMasterBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id,const char* loc);
	void captureMasterEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	//single
	void captureSingleBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id, const char* loc);
	void captureSingleEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureTaskBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t parent_task_id, ompt_task_id_t new_task_id, void *new_task_function);
	void captureTaskEnd(ompt_thread_id_t tid, ompt_task_id_t task_id);
	void captureImplicitTaskBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id,const char* loc);
	void captureImplicitTaskEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureBarrierBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureBarrierEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureTaskwaitBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureTaskwaitEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	void captureTaskAlloc(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t parent_task_id, ompt_task_id_t new_task_id, void *new_task_function, const char* loc);
	//loop
	void captureLoopBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id, const char* loc);
	void captureLoopEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id);
	//loop dynamic
	void captureLoopChunk(ompt_thread_id_t tid, int lb, int ub, int st, int last);
	void captureLoopChunk(ompt_thread_id_t tid, int last);
	int getFirstDispatch(ompt_thread_id_t tid);
	//task dependence
	/**
	 *  Capture task tid's dependency list and serialize it
	 *  Format of output is task id, n, addr_1, type_1, ..., addr_n, type_n 
	 * */
	void captureTaskDependences(ompt_thread_id_t tid, ompt_task_id_t task_id, const ompt_task_dependence_t *deps, int ndeps);
	void updateLoc(ompt_thread_id_t tid, const char* file, int line);
	//TODO merge getParent with this call?
	TreeNode* getCurrentParent(ompt_thread_id_t tid);
	void finishProfile();
	
	
};

#endif