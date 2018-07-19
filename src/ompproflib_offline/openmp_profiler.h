#ifndef OPENMP_PROFILER_H_
#define OPENMP_PROFILER_H_

#include "globals.h"
#include <ompt-regression.h>
#include "treeNode.h"
#include <sstream>
#include <algorithm>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>

#define NUM_ENTRIES 1024

typedef std::tuple<ompt_thread_id_t, ompt_parallel_id_t, ompt_task_id_t> node_id_t; 
extern std::atomic<unsigned long> node_ctr;
extern std::atomic<unsigned long> callSite_ctr;



class OpenmpProfiler{
private:
	int ompp_initialized;
	std::ofstream report[NUM_THREADS+1];
	std::ofstream tree_nodes[NUM_THREADS+1];
	std::ofstream revised_tree_nodes[NUM_THREADS+1];
	/// Log task dependencies
	std::ofstream task_dependencies[NUM_THREADS+1];
	std::ofstream callsite_info;

	std::vector<TreeNode> last_nodes[NUM_THREADS+1];
	std::map<ompt_task_id_t, TreeNode*> taskMap;
	std::map<ompt_task_id_t, std::string> shadowTaskMap;
	std::map<node_id_t, unsigned long> pendingTaskNode;
	std::map<std::string, unsigned long> callSiteMap;
	//for loop analysis
	std::map<unsigned long, bool> loopCallSites;
	//index to the finish node for current parallel region
	unsigned int parNodeIndex;
	unsigned int num_threads;
	//flag to avoid race in barrierBegin/end
	int parBeginFlag;
	int parEndFlag;
	int barBeginFlag;
	int barEndFlag;
	int barFirstThread;
	int implEndCount;
	unsigned long barrierCount[NUM_THREADS+1];
	std::vector<TreeNode> *pendingFinish;
	//ompp_loc
	//std::string latestCallSite[NUM_THREADS+1];
	std::map<std::string, int> callSitesToRemove;
	//loop dynamic
	unsigned long prevLoopCallsite[NUM_THREADS+1];
	int firstDispatch[NUM_THREADS+1];//1 denotes its the first dispatch, 0 otherwise
	
	void cleanupFinishNode(ompt_thread_id_t tid);
	//util
	int createDir(const char* dirName);
	int dirExists(const char *path);
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
	TreeNode getCurrentParent(ompt_thread_id_t tid);
	void finishProfile();
};

#endif