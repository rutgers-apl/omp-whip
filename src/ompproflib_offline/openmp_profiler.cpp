#include "openmp_profiler.h"

//set to one to get log report
#define DEBUG 0

pthread_mutex_t report_map_mutex = PTHREAD_MUTEX_INITIALIZER; 

pthread_mutex_t bar_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t par_mutex = PTHREAD_MUTEX_INITIALIZER;


OpenmpProfiler::OpenmpProfiler(){
	ompp_initialized = 0;
	//check for log directory, create if it doesn't exist
	const char *path = "./log";
	const int exist = dirExists(path);
	if (exist ==1){
		std::cout << "log directory exists" << std::endl;
	}
	else{
		std::cout << "no log directory found. creating it" << std::endl;
		const int res = createDir(path);
		if (res == 0){
			std::cout << "log directory created successfully" << std::endl;
		}
		else{
			std::cout << "failed to create directory exiting" << std::endl;
			exit(1);
		}
	}

	int num_procs = omp_get_num_procs();
	std::cout << "num procs = " << num_procs << std::endl;
	if (NUM_THREADS < num_procs){
		std::cout << "This system supports more cores than the tool has been configured to hanlde please update NUM_THREADS in globals.h" << std::endl;
		std::cout << "exiting" << std::endl;
		exit(1); 
	}
	num_threads = num_procs;
	for (unsigned int i = 1; i <= NUM_THREADS; i++) {

		std::string file_name = "log/log_" + std::to_string(i) + ".csv";
		report[i].open(file_name);

		file_name = "log/tree_" + std::to_string(i) + ".csv";
		tree_nodes[i].open(file_name);
		
		file_name = "log/rev_tree_" + std::to_string(i) + ".csv";
		revised_tree_nodes[i].open(file_name);
		
		file_name = "log/task_dep_" + std::to_string(i) + ".csv";
		task_dependencies[i].open(file_name);
		prevLoopCallsite[i] = 0;
		firstDispatch[i] = 0;

		//reset the barrier count which will get reset at the beginning of a parallel region
		barrierCount[i] = 0;
	}
	//used to push pending finish nodes from pther threads that reach a barrier
	pendingFinish = new std::vector<TreeNode>();
	parBeginFlag = 0;
	barBeginFlag = 0;
	barFirstThread = 1;
	implEndCount = 0;
	callsite_info.open("log/callsite.csv");
	
	std::cout << "profiler initialized." << std::endl;
}

int OpenmpProfiler::createDir(const char* dirName){
  const int dirErr = mkdir(dirName, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if (-1 == dirErr)
  {
    std::cout << "Error creating directory!" << std::endl;
  }
  return dirErr;

}

int OpenmpProfiler::dirExists(const char *path){
    struct stat info;
    if(stat( path, &info ) != 0)
        return 0;
    else if(info.st_mode & S_IFDIR)
        return 1;
    else
        return 0;
}

inline void OpenmpProfiler::cleanupFinishNode(ompt_thread_id_t tid){

	if (last_nodes[tid].back().finish_flag == true){
		last_nodes[tid].pop_back();
	}
	
}

void OpenmpProfiler::initProfiler(ompt_thread_id_t serial_thread_id, ompt_parallel_id_t serial_parallel_id, ompt_task_id_t serial_task_id){
	
	#if DEBUG
	report[serial_thread_id] << "inside initProfiler serial_thread_id= " << serial_thread_id 
	<< " serial_parallel_id= " << serial_parallel_id << " serial_task_id= " << serial_task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;  
	#endif
	//this will be the root node
	TreeNode root(NodeType::FINISH);
	root.incrId = ++node_ctr;
	root.id = std::make_tuple(serial_thread_id, serial_parallel_id, serial_task_id);
	root.parent_id = std::make_tuple(0,0,0);
	root.pIncrId = 0;
	root.pType = NodeType::FINISH;
	last_nodes[serial_thread_id].push_back(root);
	root.serialize(tree_nodes[serial_thread_id]);
	ompp_initialized = 1;
}

void OpenmpProfiler::captureThreadBegin(ompt_thread_id_t tid){
}

void OpenmpProfiler::captureThreadEnd(ompt_thread_id_t tid){
}

void OpenmpProfiler::captureParallelBegin(ompt_thread_id_t tid, ompt_task_id_t parent_task_id, ompt_parallel_id_t parallel_id, uint32_t requested_team_size, void *parallel_function, const char* loc)
{
	if (ompp_initialized==0){
        return;
    }
	
	#if DEBUG
	report[tid] << "begin parallel: parallel_id = " << parallel_id << ", parent_task_id = "
	 << parent_task_id << ", requested team size: " << requested_team_size 
	 << ", omp_get_thread_num()= " << omp_get_thread_num() << ",loc =" << loc <<  std::endl;
	 #endif
	
	//check to ensure that the parallel block is not using more threads than the profiler can handle
	assert (requested_team_size <= NUM_THREADS);
	if (requested_team_size > 0){
		num_threads = requested_team_size;
	} else{
		num_threads = omp_get_num_procs();
	}
	
	#if DEBUG
	std::cout << "new parallel begin requested team size = " << requested_team_size << std::endl;
	#endif

	assert(loc != nullptr);
	std::string t_str(loc);
	 
	TreeNode new_finish(NodeType::FINISH);
	new_finish.incrId = ++node_ctr;

	//callsite
	pthread_mutex_lock(&report_map_mutex);
	if (callSiteMap.find(t_str) != callSiteMap.end()){
		new_finish.callSiteId = callSiteMap[t_str];
	}
	else{
		callSiteMap[t_str] = ++callSite_ctr;
		new_finish.callSiteId = callSiteMap[t_str];
	}
	pthread_mutex_unlock(&report_map_mutex);

	new_finish.id = std::make_tuple(tid, parallel_id, parent_task_id);
	new_finish.parent_id = last_nodes[tid].back().id;
	new_finish.pType = last_nodes[tid].back().type;
	new_finish.pIncrId = last_nodes[tid].back().incrId;
	last_nodes[tid].push_back(new_finish);

	new_finish.serialize(tree_nodes[tid]);
	 
	TreeNode new_finish2(NodeType::FINISH);
	new_finish2.incrId = ++node_ctr;
	
	new_finish2.id = std::make_tuple(tid, parallel_id, parent_task_id);
	new_finish2.parent_id = last_nodes[tid].back().id;
	new_finish2.pType = last_nodes[tid].back().type;
	new_finish2.pIncrId = last_nodes[tid].back().incrId;
	last_nodes[tid].push_back(new_finish2);
	
	new_finish2.serialize(tree_nodes[tid]);
	//this is only set by the master thread and later read by other threads so there is no race 
	parNodeIndex = last_nodes[tid].size()-1;

	//if (requested_team_size>1){
		pthread_mutex_lock(&report_map_mutex);
		for (int i=1; i<=num_threads; ++i){
			//reset barrierCounts at the beginning of a parallel region
			barrierCount[i] = 0;
			pendingFinish->clear();

			if (i==tid) continue;
			assert(last_nodes[i].size() == 0);
			TreeNode grandParent = last_nodes[1].at(parNodeIndex-1);
			last_nodes[i].push_back(grandParent);
			TreeNode parent = last_nodes[1].back();
			last_nodes[i].push_back(parent);
		}
		pthread_mutex_unlock(&report_map_mutex);
	//}
	#if DEBUG
	report[tid] << "parallel begin end" << std::endl;
	#endif
	
}


void OpenmpProfiler::captureParallelEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id)
{
	if (ompp_initialized==0){
        return;
    }
	
	while(1){
		pthread_mutex_lock(&par_mutex);
		if (implEndCount<num_threads){
			pthread_mutex_unlock(&par_mutex);
			continue;
		}
		implEndCount = 0;
		pthread_mutex_unlock(&par_mutex);
		break;
	}

	#if DEBUG
	report[tid] << "end parallel: parallel_id = " << parallel_id << ", task_id = "
	 << task_id << " omp_get_thread_num()= " << omp_get_thread_num() << std::endl; 
	#endif
}

void OpenmpProfiler::captureMasterBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id, const char* loc){
	if (ompp_initialized==0){
        return;
    }

	#if DEBUG
	report[tid] << "master begin: parallel_id = " << parallel_id << ", task_id = " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << ", loc =" << loc << std::endl;
	#endif

	assert(loc != nullptr);
	std::string t_str(loc);
	
	TreeNode new_finish(NodeType::FINISH);
	new_finish.incrId = ++node_ctr;
	
	//callsite
	pthread_mutex_lock(&report_map_mutex);
	if (callSiteMap.find(t_str) != callSiteMap.end()){
		new_finish.callSiteId = callSiteMap[t_str];
	}
	else{
		callSiteMap[t_str] = ++callSite_ctr;
		new_finish.callSiteId = callSiteMap[t_str];
	}
	pthread_mutex_unlock(&report_map_mutex);

	new_finish.id = std::make_tuple(tid, parallel_id, task_id);
	new_finish.parent_id = last_nodes[tid].back().id;
	new_finish.pType = last_nodes[tid].back().type;
	new_finish.pIncrId = last_nodes[tid].back().incrId;
	last_nodes[tid].push_back(new_finish);
	new_finish.serialize(tree_nodes[tid]);
	#if DEBUG
	report[tid]<< "master beging after push last_nodes[" << tid << "]="  << last_nodes[tid]  << std::endl;
	#endif
}

void OpenmpProfiler::captureMasterEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id){
	if (ompp_initialized==0){
        return;
    }

	#if DEBUG
	report[tid] << "master end: parallel_id = " << parallel_id << ", task_id = " << task_id
	<< " omp_get_thread_num()= " << omp_get_thread_num()  << std::endl;
	#endif 
	
	assert(!last_nodes[tid].empty());

	cleanupFinishNode(tid);
	
	last_nodes[tid].pop_back();
	#if DEBUG
	report[tid]<< "master end last_nodes[" << tid << "]="  << last_nodes[tid]  << std::endl;
	#endif
}

void OpenmpProfiler::captureSingleBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id, const char* loc){
	if (ompp_initialized==0){
        return;
    }

	#if DEBUG
	report[tid] << "single begin: parallel_id = " << parallel_id << ", task_id = " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << "loc = " << loc  << std::endl;
	#endif
	
	TreeNode new_finish(NodeType::FINISH);
	new_finish.incrId = ++node_ctr;
	new_finish.id = std::make_tuple(tid, parallel_id, task_id);
	new_finish.parent_id = last_nodes[tid].back().id;
	new_finish.pType = last_nodes[tid].back().type;
	new_finish.pIncrId = last_nodes[tid].back().incrId;
	last_nodes[tid].push_back(new_finish);
	new_finish.serialize(tree_nodes[tid]);
}

void OpenmpProfiler::captureSingleEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id){
	if (ompp_initialized==0){
        return;
    }
	
	#if DEBUG
	report[tid] << "single end: parallel_id = " << parallel_id << ", task_id = " << task_id
	<< " omp_get_thread_num()= " << omp_get_thread_num()  << std::endl;
	#endif	
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	last_nodes[tid].pop_back();
}

void OpenmpProfiler::captureLoopBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id, const char* loc){
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "loop begin: parallel_id = " << parallel_id << ", task_id = " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << "loc = " << loc  << std::endl;
	#endif

	assert(loc != nullptr);
	std::string t_str(loc);
	
	TreeNode new_finish(NodeType::FINISH);
	new_finish.incrId = ++node_ctr;
	//callsite
	pthread_mutex_lock(&report_map_mutex);
	if (callSiteMap.find(t_str) != callSiteMap.end()){
		new_finish.callSiteId = callSiteMap[t_str];
	}
	else{
		callSiteMap[t_str] = ++callSite_ctr;
		loopCallSites[callSiteMap[t_str]] = true;
		new_finish.callSiteId = callSiteMap[t_str];
	}
	pthread_mutex_unlock(&report_map_mutex);
	//denotes the current loops callsiteId
	prevLoopCallsite[tid] = callSiteMap[t_str];
	firstDispatch[tid] = 1;

	new_finish.id = std::make_tuple(tid, parallel_id, task_id);
	new_finish.parent_id = last_nodes[tid].back().id;
	new_finish.pType = last_nodes[tid].back().type;
	new_finish.pIncrId = last_nodes[tid].back().incrId;
	last_nodes[tid].push_back(new_finish);
	new_finish.serialize(tree_nodes[tid]);
}

void OpenmpProfiler::captureLoopEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id){
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "loop end: parallel_id = " << parallel_id << ", task_id = " << task_id
	<< " omp_get_thread_num()= " << omp_get_thread_num()  << std::endl;
	#endif
		
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	last_nodes[tid].pop_back();
}

void OpenmpProfiler::captureLoopChunk(ompt_thread_id_t tid, int lb, int ub, int st, int last){
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "loop chunk : lb = " << lb << ", ub = " << ub << ", st = " << st 
	<< ", last = " << last << std::endl;
	#endif
	
	assert(prevLoopCallsite[tid] != 0);

	if (firstDispatch[tid]==1){//first dispatch call to get first chunk
		firstDispatch[tid]=0;
	}
	else{//followup dispatch calls
		last_nodes[tid].pop_back();
	}
	if (last!=0){//last == 0 denotes the last chunk
		TreeNode new_async(NodeType::ASYNC);
		new_async.incrId = ++node_ctr;
		//callsite
		new_async.callSiteId = prevLoopCallsite[tid];
		// the parallel_id and task_id are not important
		new_async.id = std::make_tuple(tid, 42, 42);
		new_async.parent_id = last_nodes[tid].back().id;
		new_async.pType = last_nodes[tid].back().type;
		new_async.pIncrId = last_nodes[tid].back().incrId;
		last_nodes[tid].push_back(new_async);
		new_async.serialize(tree_nodes[tid]);

	} 
	
}

void OpenmpProfiler::captureLoopChunk(ompt_thread_id_t tid, int last){
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "loop chunk others : last = " << last << std::endl;
	#endif
	
	assert(prevLoopCallsite[tid] != 0);

	if (firstDispatch[tid]==1){//first dispatch call to get first chunk
		firstDispatch[tid]=0;
	}
	else{//followup dispatch calls
		last_nodes[tid].pop_back();
	}
	if (last!=0){//last == 0 denotes the last chunk
		TreeNode new_async(NodeType::ASYNC);
		new_async.incrId = ++node_ctr;
		//callsite
		new_async.callSiteId = prevLoopCallsite[tid];
		// the parallel_id and task_id are not important
		new_async.id = std::make_tuple(tid, 42, 42);
		new_async.parent_id = last_nodes[tid].back().id;
		new_async.pType = last_nodes[tid].back().type;
		new_async.pIncrId = last_nodes[tid].back().incrId;
		last_nodes[tid].push_back(new_async);
		new_async.serialize(tree_nodes[tid]);

	} 
	
}


void OpenmpProfiler::captureTaskBegin(ompt_thread_id_t tid, ompt_parallel_id_t parallel_id ,ompt_task_id_t parent_task_id, ompt_task_id_t new_task_id, void *new_task_function)
{
	if (ompp_initialized==0){
        return;
    }
	
	#if DEBUG
	report[tid] << "task_begin: " <<  new_task_id << ", parallel id: " << parallel_id << ", parent_id: " << parent_task_id << ", new task fptr: " 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << ", task func ptr=" << new_task_function << std::endl;
	#endif
	
	pthread_mutex_lock(&report_map_mutex);
	assert(taskMap.find(new_task_id) != taskMap.end());
	TreeNode* async_node = taskMap[new_task_id];
	taskMap.erase(new_task_id);
	pthread_mutex_unlock(&report_map_mutex);
	last_nodes[tid].push_back(*async_node);
	
	//std::cout<< "task begin: last_nodes[" << tid << "]="  << *last_nodes[tid]  << std::endl; 
	
}

void OpenmpProfiler::captureTaskEnd(ompt_thread_id_t tid, ompt_task_id_t task_id)
{
	if (ompp_initialized==0){
        return;
    }
	
	#if DEBUG
	report[tid] << "task_end: " << task_id << " omp_get_thread_num()= " << omp_get_thread_num() << std::endl; 
	#endif
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	last_nodes[tid].pop_back();
}

void OpenmpProfiler::captureImplicitTaskBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id, const char* loc)
{
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "implicit task begin: parallel_id = " << parallel_id << ", task_id= " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;
	#endif

	#if DEBUG
	report[tid] << "implicit task begin: last_nodes size = " << last_nodes[tid].size() << " ,parNodeIndex =" << parNodeIndex << " , elem " << last_nodes[tid] << std::endl;
	#endif

	assert(loc != nullptr);
	std::string t_str(loc);

	TreeNode new_async(NodeType::ASYNC);
	new_async.incrId = ++node_ctr;

	new_async.id = std::make_tuple(tid, parallel_id, task_id);

	new_async.parent_id = last_nodes[tid].back().id;
	new_async.pType = last_nodes[tid].back().type;
	new_async.pIncrId = last_nodes[tid].back().incrId;
	

	last_nodes[tid].push_back(new_async);
	new_async.serialize(tree_nodes[tid]);
	#if DEBUG
	report[tid] << "implicit task begin after push: last_nodes size = " << last_nodes[tid].size() << " ,parNodeIndex =" << parNodeIndex << " , elem " << last_nodes[tid] << std::endl;
	#endif
}


void OpenmpProfiler::captureImplicitTaskEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id)
{
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "implicit task end: parallel_id = " << parallel_id << ", task_id = " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;
	#endif
	
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	
	#if DEBUG
	report[tid] << "implicit task end before trying to pop finish and async node lastNodes size= " << last_nodes[tid].size() << ", last_nodes: " << last_nodes[tid] << std::endl;
	#endif
	last_nodes[tid].pop_back();
	last_nodes[tid].pop_back();
	//added to ensure par end i > every imp end i
	assert(!last_nodes[tid].empty());
	last_nodes[tid].pop_back();

	pthread_mutex_lock(&par_mutex);
	implEndCount++;
	pthread_mutex_unlock(&par_mutex);
}

void OpenmpProfiler::captureBarrierBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id)
{
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "barrier begin: parallel_id = " << parallel_id << ", task_id= " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;
	#endif

	#if DEBUG
	report[tid] << "barrier begin: last_nodes size = " << last_nodes[tid].size() << " , elem " << last_nodes[tid] << std::endl;
	#endif
	
	assert(last_nodes[tid].back().type == NodeType::ASYNC);
	last_nodes[tid].pop_back();
	assert(last_nodes[tid].back().type == NodeType::FINISH);
	last_nodes[tid].pop_back();

	pthread_mutex_lock(&bar_mutex);
		barrierCount[tid]+=1;
		int maxCount =0;
		for (int i=1; i<=num_threads; ++i){
			if (i==tid) continue;
			if (barrierCount[i] > maxCount){
				maxCount = barrierCount[i];
			}
		}

		if (barrierCount[tid] > maxCount){//this is the first thread visiting the barrier
			assert(pendingFinish->size()+1 == barrierCount[tid]);
			TreeNode new_finish(NodeType::FINISH);
			new_finish.incrId = ++node_ctr;
			new_finish.id = std::make_tuple(tid, parallel_id, 42);
			new_finish.parent_id = last_nodes[tid].back().id;
			new_finish.pType = last_nodes[tid].back().type;
			new_finish.pIncrId = last_nodes[tid].back().incrId;
			last_nodes[tid].push_back(new_finish);
			new_finish.serialize(tree_nodes[tid]);

			pendingFinish->push_back(new_finish);

					
		}
		else{//other threads
			assert(pendingFinish->size() >= barrierCount[tid]);
			auto index = barrierCount[tid]-1;
			TreeNode parFinish = pendingFinish->at(index);
			last_nodes[tid].push_back(parFinish);
		}

		TreeNode new_async(NodeType::ASYNC);
		new_async.incrId = ++node_ctr;
		new_async.id = std::make_tuple(tid, parallel_id, task_id);
		new_async.parent_id = last_nodes[tid].back().id;
		new_async.pType = last_nodes[tid].back().type;
		new_async.pIncrId = last_nodes[tid].back().incrId;
		last_nodes[tid].push_back(new_async);
		new_async.serialize(tree_nodes[tid]);
	pthread_mutex_unlock(&bar_mutex);
		
}

void OpenmpProfiler::captureBarrierEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id)
{
	if (ompp_initialized==0){
        return;
    }
	
	#if DEBUG
	report[tid] << "barrier end: parallel_id = " << parallel_id << ", task_id = " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;
	#endif
	
}

void OpenmpProfiler::captureTaskwaitBegin(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id){
	if (ompp_initialized==0){
        return;
    }
	
	#if DEBUG
	report[tid] << "taskwait begin: tid = " << tid << ", parallel_id = " << parallel_id << ", task_id= " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;
	#endif
	
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	
	
	#if DEBUG
	report[tid]<< "taskwait end after pop last_nodes[" << tid << "]="  << last_nodes[tid]  << std::endl;
	#endif
	
}

void OpenmpProfiler::captureTaskwaitEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id){
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "taskwait end: parallel_id = " << parallel_id << ", task_id = " << task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;
	#endif
	
}

void OpenmpProfiler::captureTaskAlloc(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t parent_task_id, ompt_task_id_t new_task_id, void *new_task_function, const char* loc){
	if (ompp_initialized==0){
        return;
    }
	#if DEBUG
	report[tid] << "task alloc: " <<  new_task_id << ", parallel id: " << parallel_id << ", parent_id: " << parent_task_id << ", new task fptr: " 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << ", task func ptr=" << new_task_function << std::endl;
	#endif

	TreeNode* new_async = nullptr;
	assert(loc !=nullptr);
	std::string t_str(loc);
		
	if (last_nodes[tid].back().young_ns_child == ASYNC){
		new_async = new TreeNode(NodeType::ASYNC);	
		new_async->incrId = ++node_ctr;
		//callsite
		pthread_mutex_lock(&report_map_mutex);
		shadowTaskMap[new_task_id] = t_str;
		if (callSiteMap.find(t_str) != callSiteMap.end() ){
			new_async->callSiteId = callSiteMap[t_str];
			
		}
		else{
			
			callSiteMap[t_str] = ++callSite_ctr;
			
			new_async->callSiteId = callSiteMap[t_str];
		}
		pthread_mutex_unlock(&report_map_mutex);
		new_async->id = std::make_tuple(tid, parallel_id, new_task_id);
		new_async->parent_id = last_nodes[tid].back().id;
		new_async->pType = last_nodes[tid].back().type;
		new_async->pIncrId = last_nodes[tid].back().incrId;
		pthread_mutex_lock(&report_map_mutex);
		taskMap[new_task_id] = new_async;
		pthread_mutex_unlock(&report_map_mutex);
		
		new_async->serializeTask(tree_nodes[tid], new_task_id);
	} 
	else{
		//create new finish node since it wasn't created yet
		TreeNode new_finish(NodeType::FINISH);
		new_finish.incrId = ++node_ctr;
		new_finish.id = std::make_tuple(tid, parallel_id, parent_task_id);
		new_finish.parent_id = last_nodes[tid].back().id;
		new_finish.pType = last_nodes[tid].back().type;
		new_finish.pIncrId = last_nodes[tid].back().incrId;
		new_finish.young_ns_child = NodeType::ASYNC;
		new_finish.finish_flag = true;
		last_nodes[tid].push_back(new_finish);
		new_finish.serialize(tree_nodes[tid]);

		
		new_async = new TreeNode(NodeType::ASYNC);	
		new_async->incrId = ++node_ctr;
		//callsite
		pthread_mutex_lock(&report_map_mutex);
		//latestCallSite[tid] = t_str;
		shadowTaskMap[new_task_id] = t_str;
		if (callSiteMap.find(t_str) != callSiteMap.end() ){
			new_async->callSiteId = callSiteMap[t_str];
			
		}
		else{
			
			callSiteMap[t_str] = ++callSite_ctr;
			new_async->callSiteId = callSiteMap[t_str];
		}
		pthread_mutex_unlock(&report_map_mutex);
		new_async->id = std::make_tuple(tid, parallel_id, new_task_id);
		new_async->parent_id = last_nodes[tid].back().id;
		new_async->pType = last_nodes[tid].back().type;
		new_async->pIncrId = last_nodes[tid].back().incrId;
		pthread_mutex_lock(&report_map_mutex);
		taskMap[new_task_id] = new_async;
		pthread_mutex_unlock(&report_map_mutex);

		new_async->serializeTask(tree_nodes[tid], new_task_id);

	}

}

void OpenmpProfiler::updateLoc(ompt_thread_id_t tid, const char* file, int line){
	if (ompp_initialized==0){
        return;
    }
	//need to create a map from tid to ompttid as this doesn't scale
	tid = tid+1;
	
	assert(!last_nodes[tid].empty());

	assert(last_nodes[tid].back().type == NodeType::ASYNC);
	
	TreeNode revisedAsync= last_nodes[tid].back();
	unsigned long remapId = last_nodes[tid].back().callSiteId;
	std::ostringstream oss;
	oss << file << ":" << line;
 	std::string newLoc = oss.str();
	
	std::string	originalLoc;
	pthread_mutex_lock(&report_map_mutex);
	
	node_id_t node_id =  revisedAsync.id;
	ompt_task_id_t task_id = std::get<2>(node_id);
	assert(shadowTaskMap.find(task_id) != shadowTaskMap.end());
	if (callSitesToRemove.count(shadowTaskMap[task_id]) == 0){
		#if DEBUG
		std::cout << "update loc: removing the following callsite : " << shadowTaskMap[task_id] << std::endl;
		#endif
		callSitesToRemove.insert(std::pair<std::string, int>(shadowTaskMap[task_id], 1));
	}
	shadowTaskMap.erase(task_id);

	if (callSiteMap.find(newLoc) != callSiteMap.end()){
		revisedAsync.callSiteId = callSiteMap[newLoc];	
		revisedAsync.serialize(revised_tree_nodes[tid]);
	} else{
		callSiteMap[newLoc] = ++callSite_ctr;
		revisedAsync.callSiteId = callSiteMap[newLoc];	
		revisedAsync.serialize(revised_tree_nodes[tid]);
	}

	pthread_mutex_unlock(&report_map_mutex);
	
}


int OpenmpProfiler::getFirstDispatch(ompt_thread_id_t tid){
	return firstDispatch[tid];
}

void OpenmpProfiler::captureTaskDependences(ompt_thread_id_t tid, ompt_task_id_t task_id, const ompt_task_dependence_t *deps, int ndeps){
  if (ompp_initialized==0){
        return;
    }
  task_dependencies[tid] << task_id << "," << ndeps;
  for(int i=0; i< ndeps; i++){
	  task_dependencies[tid] << "," << std::hex << (uintptr_t)deps[i].variable_addr << std::dec << "," << (int)deps[i].dependence_flags;
  }
  task_dependencies[tid] << std::endl;

}

TreeNode OpenmpProfiler::getCurrentParent(ompt_thread_id_t tid){
	return last_nodes[tid].back();
}

void OpenmpProfiler::finishProfile(){

	std::cout << "finishing profiler" << std::endl;

	//serialize callSiteMap,
	for (auto elem: callSiteMap){
		bool flag = false;
		for(auto removeElem : callSitesToRemove){
			if (elem.first == removeElem.first){
				flag = true;
				break;
			}

		}
		if (!flag){
			if (loopCallSites.find(elem.second) != loopCallSites.end()){
				callsite_info << elem.first << ", " << elem.second << "L" << std::endl;
			}
			else{
				callsite_info << elem.first << ", " << elem.second << std::endl;
			}
			
		}
		else{
			#if DEBUG
			std::cout << "[temp log] finish profiler. Not adding the this callsite: " << elem.first << std::endl;
			#endif
		}
	}

	for (unsigned int i = 1; i <= NUM_THREADS; i++) {
		#if DEBUG
		report[i] << "end thread" << std::endl;
		#endif
		report[i].close();
		tree_nodes[i].close();
		revised_tree_nodes[i].close();
		task_dependencies[i].close();

	}
	callsite_info.close();
	ompp_initialized = 0;
}
