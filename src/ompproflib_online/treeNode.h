#ifndef TREE_NODE_H_
#define TREE_NODE_H_

#include "globals.h"
#include <tuple>

enum NodeType {
    NO_TYPE = 0,
    ASYNC = 1,
    FINISH = 2,
    STEP = 3
};

class TreeNode{
public:
    NodeType type;
    TreeNode* parent_ptr{nullptr};
    //NodeType pType;
    unsigned long incrId;
	//unsigned long pIncrId;
    unsigned long callSiteId;
    NodeType young_ns_child;
    bool finish_flag;    
    ompt_task_id_t task_id;
    //needed for loop callsites
    ompt_parallel_id_t parallel_id{0};
    //online profiling items
    bool is_barrier_finish{false};
    std::atomic<unsigned long long> child_count{0};
    std::atomic<unsigned long long> work{0};
    //std::atomic<unsigned long long> left_work{0};
    std::atomic<unsigned long long> left_finish_e_work{0};
    std::atomic<unsigned long long> left_step_work{0};
    std::atomic<unsigned long long> left_finish_c_work{0};
    std::atomic<unsigned long long> p_left_sibling_work{0};
    std::atomic<unsigned long long> p_left_sibling_e_work{0};
    std::atomic<unsigned long long> c_work{0};
    std::atomic<unsigned long long> e_work{0};
    pthread_mutex_t object_lock;
    std::map<unsigned long, long> *ssList{nullptr};
    std::map<unsigned long, long> *p_ssList{nullptr};
    std::map<unsigned long, long> *l_ssList{nullptr};
    std::map<unsigned long, long> *regionWork{nullptr};

    TreeNode();
    TreeNode(NodeType node_type);
    TreeNode(TreeNode* node);
    void serialize(std::ofstream& strm);
    void serializeTask(std::ofstream& strm, uint64_t task_id);
    friend std::ostream& operator<<(std::ostream& os, const TreeNode& node);
    friend std::ostream& operator<<(std::ostream& os, const TreeNode* node);
};

#endif
