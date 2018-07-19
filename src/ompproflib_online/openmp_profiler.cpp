#include "openmp_profiler.h"

//set to one to get log report
#define DEBUG 0

pthread_mutex_t report_map_mutex = PTHREAD_MUTEX_INITIALIZER; 

pthread_mutex_t bar_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t bar_finish_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t par_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t all_callsites_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t debug_log_mutex = PTHREAD_MUTEX_INITIALIZER;


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
		prevLoopCallsite[i] = 0;
		firstDispatch[i] = 0;
		barrierCount[i] = 0;
	}
	//used to push pending finish nodes from other threads that reach a barrier
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

	if (last_nodes[tid].back()->finish_flag == true){
		popNode(tid);
	}
	
}

void OpenmpProfiler::cleanupNode(TreeNode* node){
	delete node->regionWork;
	delete node;
}


void OpenmpProfiler::initProfiler(ompt_thread_id_t serial_thread_id, ompt_parallel_id_t serial_parallel_id, ompt_task_id_t serial_task_id){
	#if DEBUG
	report[serial_thread_id] << "inside initProfiler serial_thread_id= " << serial_thread_id 
	<< " serial_parallel_id= " << serial_parallel_id << " serial_task_id= " << serial_task_id 
	<< " omp_get_thread_num()= " << omp_get_thread_num() << std::endl;  
	#endif

	TreeNode* root = new TreeNode(NodeType::FINISH);
	root->parent_ptr = nullptr;
	root->incrId = ++node_ctr;

	pushRootNode(serial_thread_id, root);
	ompp_initialized = 1;
}

void OpenmpProfiler::pushRootNode(ompt_thread_id_t tid, TreeNode* node){
	#if DEBUG
	std::cout << "pushNode root: node_id = " << node->incrId << ", node type = " << node->type << std::endl; 
	#endif

	node->p_left_sibling_work = 0;
	//is this lock necessary?
	pthread_mutex_lock(&node->object_lock);
	node->ssList = new std::map<unsigned long, long>;
	node->p_ssList = new std::map<unsigned long, long>;
	node->l_ssList = new std::map<unsigned long, long>;
	node->regionWork = new std::map<unsigned long, long>;
    pthread_mutex_unlock(&node->object_lock);
	last_nodes[tid].push_back(node);

}

void OpenmpProfiler::pushNode(ompt_thread_id_t tid, TreeNode* node, bool is_barrier_finish=false){


	TreeNode* parent = node->parent_ptr;
	assert(parent!=nullptr);
	node->p_left_sibling_work = parent->left_step_work + parent->left_finish_c_work;
	node->p_left_sibling_e_work = parent->left_step_work + parent->left_finish_e_work;

	#if DEBUG
	pthread_mutex_lock(&debug_log_mutex);
	std::cout << "pushNode: node_id = " << node->incrId << ", node type = " << node->type << ", node callSite = "
	<< node->callSiteId 
	<< ", parent_id = " << node->parent_ptr->incrId << ", parent type = " << node->parent_ptr->type  
	<< ", parent callSite= " << node->parent_ptr->callSiteId << ", node p_left_sibling_work = " 
	<< node->p_left_sibling_work << ", node p_left_sibling_e_work = " <<  node->p_left_sibling_e_work
	<<  std::endl; 
	pthread_mutex_unlock(&debug_log_mutex);
	#endif

	//is this lock necessary?
	pthread_mutex_lock(&node->object_lock);
	node->is_barrier_finish = is_barrier_finish;
	node->ssList = new std::map<unsigned long, long>;
	node->p_ssList = new std::map<unsigned long, long>;
	node->l_ssList = new std::map<unsigned long, long>;
	CopySSList(node->p_ssList, parent->l_ssList);
	node->regionWork = new std::map<unsigned long, long>;
    pthread_mutex_unlock(&node->object_lock);
	last_nodes[tid].push_back(node);
}

TreeNode* OpenmpProfiler::popNode(ompt_thread_id_t tid){
	assert(!last_nodes[tid].empty());
	TreeNode* node = last_nodes[tid].back();
	TreeNode* parent_node = node->parent_ptr;
	
	#if DEBUG
	pthread_mutex_lock(&debug_log_mutex);
	std::cout << "popNode: node_id = " << node->incrId << ", node type = " << node->type 
	<< ", node callSite = " << node->callSiteId << std::endl; 
	pthread_mutex_unlock(&debug_log_mutex);
	#endif
	
	if (node->is_barrier_finish){
		pthread_mutex_lock(&bar_finish_mutex);
		node->child_count++;
		if(node->child_count != num_threads){//do not pop until all children have been popped
			pthread_mutex_unlock(&bar_finish_mutex);
			//parent_node->child_count++;
			last_nodes[tid].pop_back();
			return node;
		}
		pthread_mutex_unlock(&bar_finish_mutex);
	}
	
	//check if the sum of all finish and step nodes is the critical path
	if (node->c_work < node->left_finish_c_work + node->left_step_work){
		node->c_work = node->left_finish_c_work + node->left_step_work;
		node->e_work = node->left_finish_e_work + node->left_step_work;
		node->ssList->clear();
		delete node->ssList;
		node->ssList = node->l_ssList;
	}
	else{
		node->l_ssList->clear();
		delete node->l_ssList;
	}

	if (node->callSiteId !=0){
		//doesn't need locking as it happens during a pop
		UpdateSSList(node->callSiteId, node->e_work, node->ssList);
	}

	#if DEBUG
	pthread_mutex_lock(&debug_log_mutex);
	std::cout << "work measure popNode: node_id = " << node->incrId << ", node type = " 
	<< node->type << ", node work= " << node->work << ", node c_work= " << node->c_work 
	<< ", node e_work= " << node->e_work << std::endl; 
	//sum(node.ssList.c_work)  == node.c_work
	long ssList_sum = 0;
	for(auto elem: *(node->ssList)){
		ssList_sum+=elem.second;
	}
	if (node->callSiteId == 0)
		ssList_sum+=node->e_work;
	long diff = ssList_sum - node->c_work;
	if(diff != 0){
		std::cout << "[error] node id = " << node->incrId << " ssList_sum = " << ssList_sum 
		<< " , c_work = " << node->c_work << " do not match. difference = " 
		<< diff << std::endl;   
	}
	pthread_mutex_unlock(&debug_log_mutex);
	#endif	

	//invariant node->work,c_work, e_work are already set to their final value
	if (node->callSiteId !=0){
		bool found = false;
		TreeNode* ancestor = node->parent_ptr;
		while (ancestor!=nullptr){
			if (ancestor->callSiteId == node->callSiteId){
				found = true;
				break;
			}
			ancestor = ancestor->parent_ptr;
		}
		if(!found){
			pthread_mutex_lock(&all_callsites_mutex);
			if (allCallSites.count(node->callSiteId) == 0){
				CallSiteWork csWork;
        		csWork.work = 0;
        		csWork.cWork = 0;
				allCallSites.insert(std::pair<unsigned long, CallSiteWork>(node->callSiteId, csWork));
            }
			allCallSites.at(node->callSiteId).work += node->work;
			if (loopCallSites.count(node->callSiteId) == 1){
                std::map<unsigned long, long long>& loopWork = loopInstances[node->callSiteId];
                if (loopWork.count(node->parallel_id) == 0){
                    loopWork.insert(std::pair<unsigned long, long long>(node->parallel_id,0));
                }
                if (loopWork.at(node->parallel_id) < node->c_work){
                    loopWork.at(node->parallel_id) = node->c_work;
                }
            }
            else{
                allCallSites.at(node->callSiteId).cWork += node->c_work;
            }
			pthread_mutex_unlock(&all_callsites_mutex);
		}

	}
	

	if (node->type == NodeType::FINISH){
		pthread_mutex_lock(&parent_node->object_lock);
		if(node->callSiteId == 0){
			parent_node->left_finish_e_work+= node->e_work;
		}
		parent_node->left_finish_c_work+=node->c_work;
		parent_node->work+=node->work; 
		
		CopySSList(parent_node->l_ssList,node->ssList);
		node->ssList->clear();
		delete node->ssList;
		node->p_ssList->clear();
		delete node->p_ssList;
			
		pthread_mutex_unlock(&parent_node->object_lock);		
	}
	else if(node->type == NodeType::ASYNC){
		pthread_mutex_lock(&parent_node->object_lock);
		parent_node->work+=node->work;
		if(node->p_left_sibling_work + node->c_work > parent_node->c_work){
			CopySSList(node->p_ssList,node->ssList);
			node->ssList->clear();
			delete node->ssList;

			parent_node->ssList = node->p_ssList;
			parent_node->c_work = node->p_left_sibling_work + node->c_work;
			if (node->callSiteId==0){
				parent_node->e_work = node->e_work + node->p_left_sibling_e_work;
			}
			else{
				parent_node->e_work = node->p_left_sibling_e_work + 0;
			}
		}
		else{
			node->ssList->clear();
			delete node->ssList;
			node->p_ssList->clear();
			delete node->p_ssList;
			
		}
		pthread_mutex_unlock(&parent_node->object_lock);
	}	
	else{
		std::cout << "popping an unidentified node type! node_id = " << node->incrId << " , node_type = " << node->type
		<< std::endl;
		assert(false);
	}

	cleanupNode(node);
	last_nodes[tid].pop_back();
	return node;
}

void OpenmpProfiler::UpdateSSList(unsigned long callSiteId, long eWork, std::map<unsigned long, long> *ssList){
    if(ssList->count(callSiteId) !=0){
        ssList->at(callSiteId) += eWork;
    }
    else{
        ssList->insert(std::pair<unsigned long,long>(callSiteId, eWork));
    }
}

void OpenmpProfiler::CopySSList(std::map<unsigned long, long> *dst, std::map<unsigned long, long> *src){
    for(auto sElem: *src){
        UpdateSSList(sElem.first, sElem.second, dst);
    }

}

TreeNode* OpenmpProfiler::getParent(ompt_thread_id_t tid){
	return last_nodes[tid].back();	
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

	TreeNode* new_finish = new TreeNode(NodeType::FINISH);
	new_finish->incrId = ++node_ctr;

	//callsite
	assert(loc != nullptr);
	std::string t_str(loc);
	pthread_mutex_lock(&report_map_mutex);
	if (callSiteMap.find(t_str) != callSiteMap.end()){
		new_finish->callSiteId = callSiteMap[t_str];
	}
	else{
		callSiteMap[t_str] = ++callSite_ctr;
		new_finish->callSiteId = callSiteMap[t_str];
	}
	pthread_mutex_unlock(&report_map_mutex);

	new_finish->parent_ptr = getParent(tid);
	pushNode(tid, new_finish);
	
	TreeNode* new_finish2 = new TreeNode(NodeType::FINISH);
	new_finish2->incrId = ++node_ctr;
	new_finish2->parent_ptr = getParent(tid);
	pushNode(tid, new_finish2, true);
	
	//this is only set by the master thread and later read by other threads so there is no race 
	int  parNodeIndex = last_nodes[tid].size()-1;

	//copy the the two finish nodes in master to all other threads in the team
	pthread_mutex_lock(&report_map_mutex);
		for (int i=1; i<=num_threads; ++i){
			//reset barrierCounts at the beginning of a parallel region
			barrierCount[i] = 0;
			pendingFinish.clear();

			if (i==tid) continue;
			last_nodes[i].clear();
			assert(last_nodes[i].size() == 0);
			TreeNode* grand_parent = last_nodes[1].at(parNodeIndex-1);
			last_nodes[i].push_back(grand_parent);
			TreeNode* parent = last_nodes[1].back();
			last_nodes[i].push_back(parent);
			
		}
		pthread_mutex_unlock(&report_map_mutex);
	
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

	//pop finish associated with parallel region
	#if DEBUG
	std::cout << "parallel end poping parallel finish. node_id= " << last_nodes[tid].back()->incrId << std::endl;
	#endif 

	popNode(tid);


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
	
	TreeNode* new_finish = new TreeNode(NodeType::FINISH);
	new_finish->incrId = ++node_ctr;
	
	//callsite
	pthread_mutex_lock(&report_map_mutex);
	if (callSiteMap.find(t_str) != callSiteMap.end()){
		new_finish->callSiteId = callSiteMap[t_str];
	}
	else{
		callSiteMap[t_str] = ++callSite_ctr;
		new_finish->callSiteId = callSiteMap[t_str];
	}
	pthread_mutex_unlock(&report_map_mutex);

	new_finish->parent_ptr = getParent(tid);
	pushNode(tid, new_finish);
	
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
	#if DEBUG
	report[tid]<< "master end start last_nodes[" << tid << "]="  << last_nodes[tid]  << std::endl;
	#endif
	
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	popNode(tid);
	
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
	
	TreeNode* new_finish = new TreeNode(NodeType::FINISH);
	new_finish->incrId = ++node_ctr;
	
	new_finish->parent_ptr = getParent(tid);
	pushNode(tid, new_finish);
	
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
	popNode(tid);

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

	TreeNode* new_finish = new TreeNode(NodeType::FINISH);
	new_finish->incrId = ++node_ctr;
	new_finish->parallel_id = parallel_id;
	//callsite
	pthread_mutex_lock(&report_map_mutex);
	if (callSiteMap.find(t_str) != callSiteMap.end()){
		new_finish->callSiteId = callSiteMap[t_str];
	}
	else{
		callSiteMap[t_str] = ++callSite_ctr;
		loopCallSites[callSiteMap[t_str]] = true;
		new_finish->callSiteId = callSiteMap[t_str];
	}
	pthread_mutex_unlock(&report_map_mutex);
	//denotes the current loops callsiteId
	prevLoopCallsite[tid] = callSiteMap[t_str];
	firstDispatch[tid] = 1;
	new_finish->parent_ptr = getParent(tid);
	pushNode(tid, new_finish);
	
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
	popNode(tid);
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
		popNode(tid);
	}

	if (last!=0){//last == 0 denotes the last chunk
		TreeNode* new_async = new TreeNode(NodeType::ASYNC);
		new_async->incrId = ++node_ctr;
		new_async->callSiteId = prevLoopCallsite[tid];
		new_async->parent_ptr = getParent(tid);
		pushNode(tid, new_async);
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
		popNode(tid);
	}

	if (last!=0){//last == 0 denotes the last chunk
		TreeNode* new_async = new TreeNode(NodeType::ASYNC);
		new_async->incrId = ++node_ctr;
		new_async->callSiteId = prevLoopCallsite[tid];
		new_async->parent_ptr = getParent(tid);
		pushNode(tid, new_async);
	} 
	
}


void OpenmpProfiler::captureTaskBegin(ompt_thread_id_t tid, ompt_parallel_id_t parallel_id ,ompt_task_id_t parent_task_id, ompt_task_id_t new_task_id, void *new_task_function)
{
	if (ompp_initialized==0){
        return;
    }
		
	pthread_mutex_lock(&report_map_mutex);
	assert(taskMap.find(new_task_id) != taskMap.end());
	TreeNode* async_node = taskMap[new_task_id];
	taskMap.erase(new_task_id);
	pthread_mutex_unlock(&report_map_mutex);
	pushNode(tid,async_node);
	
	
}

void OpenmpProfiler::captureTaskEnd(ompt_thread_id_t tid, ompt_task_id_t task_id)
{
	if (ompp_initialized==0){
        return;
    }
	
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	popNode(tid);
	
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

	TreeNode* new_async = new TreeNode(NodeType::ASYNC);
	new_async->incrId = ++node_ctr;
	new_async->parent_ptr = getParent(tid);
	pushNode(tid, new_async);
	
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
	std::cout << "implicit task end before poping tid[" << tid << "] = " << last_nodes[tid] << std::endl;
	report[tid] << "implicit task end before trying to pop finish and async node lastNodes size= " << last_nodes[tid].size() << ", last_nodes: " << last_nodes[tid] << std::endl;
	#endif
	//pop async representing implicit task
	#if DEBUG
	std::cout << "implicit task end poping asycn. node_id= " << last_nodes[tid].back()->incrId << std::endl;
	#endif
	popNode(tid);
	//pop finish associated with current barrier
	#if DEBUG
	std::cout << "implicit task end poping barrier finish. node_id= " << last_nodes[tid].back()->incrId << std::endl;
	#endif 
	popNode(tid);

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
	
	assert(last_nodes[tid].back()->type == NodeType::ASYNC);
	popNode(tid);
	assert(last_nodes[tid].back()->type == NodeType::FINISH);
	popNode(tid);
	
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
			assert(pendingFinish.size()+1 == barrierCount[tid]);
			TreeNode* new_finish = new TreeNode(NodeType::FINISH);
			new_finish->incrId = ++node_ctr;
			new_finish->parent_ptr = getParent(tid);
			#if DEBUG
			std::cout << "barrier begin first thread pushing finish. node_id = " << new_finish->incrId << std::endl;
			#endif
			pushNode(tid, new_finish, true);
			pendingFinish.push_back(new_finish);

		}
		else{//other threads
			assert(pendingFinish.size() >= barrierCount[tid]);
			auto index = barrierCount[tid]-1;
			TreeNode* par_finish = pendingFinish.at(index);
			//TODO this probably needs fixing
			#if DEBUG
			std::cout << "barrier begin thread pushing finish. node_id = " << par_finish->incrId << std::endl;
			#endif
			last_nodes[tid].push_back(par_finish);	
			
		}
		
		TreeNode* new_async = new TreeNode(NodeType::ASYNC);
		new_async->incrId = ++node_ctr;
		new_async->parent_ptr = getParent(tid);
		pushNode(tid, new_async);
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
	
	#if DEBUG
	report[tid]<< "taskwait end after pop last_nodes[" << tid << "]="  << last_nodes[tid]  << std::endl;
	#endif
	
}

void OpenmpProfiler::captureTaskwaitEnd(ompt_thread_id_t tid,ompt_parallel_id_t parallel_id, ompt_task_id_t task_id){
	if (ompp_initialized==0){
        return;
    }
	
	assert(!last_nodes[tid].empty());
	cleanupFinishNode(tid);
	
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
	
	if (last_nodes[tid].back()->young_ns_child == ASYNC){
		new_async = new TreeNode(NodeType::ASYNC);	
		new_async->incrId = ++node_ctr;
		new_async->task_id = new_task_id;
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
		new_async->parent_ptr = getParent(tid);
		pthread_mutex_lock(&report_map_mutex);
		taskMap[new_task_id] = new_async;
		pthread_mutex_unlock(&report_map_mutex);
		
	} 
	else{
		//create new finish node since it wasn't created yet
		TreeNode* new_finish = new TreeNode(NodeType::FINISH);
		new_finish->incrId = ++node_ctr;
		
		new_finish->young_ns_child = NodeType::ASYNC;
		new_finish->finish_flag = true;
		new_finish->parent_ptr = getParent(tid);
		pushNode(tid, new_finish);
		
		new_async = new TreeNode(NodeType::ASYNC);	
		new_async->incrId = ++node_ctr;
		new_async->task_id = new_task_id;
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
		
		new_async->parent_ptr = getParent(tid);
		pthread_mutex_lock(&report_map_mutex);
		taskMap[new_task_id] = new_async;
		pthread_mutex_unlock(&report_map_mutex);


	}

}

void OpenmpProfiler::updateLoc(ompt_thread_id_t tid, const char* file, int line){
	if (ompp_initialized==0){
        return;
    }
	//need to create a map from tid to ompttid as this doesn't scale
	tid = tid+1;
	
	assert(!last_nodes[tid].empty());
	
	assert(last_nodes[tid].back()->type == NodeType::ASYNC);
	//assert(last_nodes[tid].back().callSiteId !=0);
	
	TreeNode* revisedAsync= last_nodes[tid].back();
	unsigned long remapId = last_nodes[tid].back()->callSiteId;
	//this is a o(n) traversal should think of a better solution
	std::ostringstream oss;
	oss << file << ":" << line;
 	std::string newLoc = oss.str();
	std::string	originalLoc;
	pthread_mutex_lock(&report_map_mutex);
	ompt_task_id_t task_id = revisedAsync->task_id;
	assert(shadowTaskMap.find(task_id) != shadowTaskMap.end());
	if (callSitesToRemove.count(shadowTaskMap[task_id]) == 0){
		#if DEBUG
		std::cout << "update loc: removing the following callsite : " << shadowTaskMap[task_id] << std::endl;
		#endif
		callSitesToRemove.insert(std::pair<std::string, unsigned long>(shadowTaskMap[task_id], 1));
	}
	shadowTaskMap.erase(task_id);

	if (callSiteMap.find(newLoc) != callSiteMap.end()){
		revisedAsync->callSiteId = callSiteMap[newLoc];	
	} else{
		callSiteMap[newLoc] = ++callSite_ctr;
		revisedAsync->callSiteId = callSiteMap[newLoc];	
	}

	pthread_mutex_unlock(&report_map_mutex);
	
}


int OpenmpProfiler::getFirstDispatch(ompt_thread_id_t tid){
	return firstDispatch[tid];
}
//TODO figure this out later for online setting
void OpenmpProfiler::captureTaskDependences(ompt_thread_id_t tid, ompt_task_id_t task_id, const ompt_task_dependence_t *deps, int ndeps){
  /**
  if (ompp_initialized==0){
        return;
    }
  task_dependencies[tid] << task_id << "," << ndeps;
  for(int i=0; i< ndeps; i++){
	  task_dependencies[tid] << "," << std::hex << (uintptr_t)deps[i].variable_addr << std::dec << "," << (int)deps[i].dependence_flags;
  }
  task_dependencies[tid] << std::endl;
  **/
}

TreeNode* OpenmpProfiler::getCurrentParent(ompt_thread_id_t tid){
	return last_nodes[tid].back();
}
void OpenmpProfiler::GenerateProfile(){
	//root node resides on the master's thread stack
	//TreeNode* root = popNode(1);
	TreeNode* root = last_nodes[1].back();

	//update root's parallelism profile
	if (root->c_work < root->left_finish_c_work + root->left_step_work){
		root->c_work = root->left_finish_c_work + root->left_step_work;
		root->e_work = root->left_finish_e_work + root->left_step_work;
		root->ssList = root->l_ssList;
	}
	//update critical work for loops
	for(auto elem: loopInstances){
        unsigned long loopCallSite = elem.first;
        long long loopCWork= 0;
        for (auto loopElem: elem.second){
            loopCWork+=loopElem.second;
        }
        allCallSites[loopCallSite].cWork = loopCWork;
    }
	std::string file_name = "log/profiler_output.csv";
	std::ofstream outputFile;
    outputFile.open(file_name);

	long long work = root->work;
    long long cWork = root->c_work;
    long long eWork = root->e_work;

	std::unordered_map<unsigned long, std::string> callSiteIdtoLoc;
	for(auto elem: callSiteMap){
		callSiteIdtoLoc.insert(std::pair<unsigned long, std::string>(elem.second,elem.first));
	}

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
        if (verbose == true){
            if (callSiteIdtoLoc[elem.first].back() == ';'){
                outputFile << callSiteIdtoLoc[elem.first].substr(1, callSiteIdtoLoc[elem.first].length()-3);
            }
            else{
                outputFile << callSiteIdtoLoc[elem.first]; 
            }
            
            outputFile << ", " << elem.second.work << ", " << elem.second.cWork
            << ", " << static_cast<double>(elem.second.work)/elem.second.cWork;
            auto ssList = root->ssList;
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
            auto ssList = root->ssList;    
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
	long ssList_cwork_sum = 0;
    auto ssList = root->ssList;
    std::cout << "-----------------------" << std::endl;
    std::cout << "ssList count" << ssList->size() << std::endl;
    for(auto elem:*ssList){
        std::cout << "callSite = " << callSiteIdtoLoc[elem.first] << std::endl;
        std::cout << "work on critical path = " << elem.second << std::endl;
        std::cout << "critcal path percentage = " << static_cast<double>(elem.second)/cWork << std::endl;
        double temp = static_cast<double>(elem.second)/cWork;
		ssList_cwork_sum += elem.second;
        allSitesCPP += temp;
    }
    //delete root ssList
	ssList->clear();
    delete ssList;
    std::cout << "analysis done!" << std::endl;
    std::cout << "program work = " << work << std::endl;
    std::cout << "critical work = " << cWork << std::endl;
	ssList_cwork_sum+=eWork;
	std::cout << "x = sum of ssList critical work + main.e_work = " << ssList_cwork_sum << std::endl;
	std::cout << "program.c_work - x == " << cWork - ssList_cwork_sum << std::endl;
    std::cout << "program exclusive work = " << eWork << std::endl;
    std::cout << "main critical path percentage = " << static_cast<double>(eWork)/cWork << std::endl;
    std::cout << "parallelism = " << static_cast<double>(work)/cWork << std::endl;
    double temp = static_cast<double>(eWork)/cWork;
    //std::cout << "temp= " << temp << std::endl;
    allSitesCPP += temp;
    std::cout << "sum of all critical paths = " << allSitesCPP << std::endl;
	
	outputFile.flush();
    outputFile.close();
}

void OpenmpProfiler::finishProfile(){
	

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
	for(auto elem:callSitesToRemove){
		if (callSiteMap.count(elem.first)>0){
			callSiteMap.erase(elem.first);
		}
	}

	std::cout << "finishing profiler" << std::endl;
	std::cout << "generating profiler output" << std::endl;
	GenerateProfile();
	std::cout << "profile generation complete" << std::endl;

	callsite_info.close();
	ompp_initialized = 0;
}
