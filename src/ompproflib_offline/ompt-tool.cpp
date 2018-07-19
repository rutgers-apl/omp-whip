
//*****************************************************************************
// system includes
//*****************************************************************************

#include <map>
#include <set>
#include "openmp_profiler.h"
#include "perf_profiler.h"
#include "causalProfiler.h"
#include "ompt.h"
//*****************************************************************************
// OpenMP runtime includes
//*****************************************************************************

#include <omp.h>



//*****************************************************************************
// regression harness includes
//*****************************************************************************

#include "ompt-regression.h"
#include "ompt-initialize.h"



//*****************************************************************************
// macros
//*****************************************************************************

#define DEBUG 0


//*****************************************************************************
// global variables
//*****************************************************************************

//OpenmpProfiler omp_profiler;
//PerfProfiler perf_profiler;
static std::map<ompt_parallel_id_t, ompt_task_id_t> parallel_id_to_task_id;
static std::set<ompt_task_id_t> task_ids;

//std::atomic<unsigned long> node_ctr(0);
//
int regions_active = 0;

ompt_task_id_t serial_task_id;
ompt_frame_t * serial_task_frame;

//for task begin end
std::map<ompt_task_id_t, ompt_task_id_t> task_id_to_task_id_map;
std::map<ompt_task_id_t, ompt_frame_t *> task_id_to_task_frame_map;
int tasks_active = 0;
 

void dump_chain(int depth)
{
  ompt_task_id_t task_id = ompt_get_task_id(depth);
#if DEBUG
  printf("level %d: task %" PRIu64 "\n", depth, task_id);
#endif
  if (task_id != 0) dump_chain(depth+1); 
}


//*****************************************************************************
// private operations 
//*****************************************************************************


static void 
on_ompt_event_thread_begin
(ompt_thread_type_t thread_type, /* type of thread */
ompt_thread_id_t thread_id /* ID of thread */
)
{
#if DEBUG
  //printf("thread begin= %" PRIu64 "\n", thread_id);
#endif
  omp_profiler.captureThreadBegin(thread_id);
}

static void
on_ompt_event_parallel_begin
(ompt_task_id_t parent_task_id,    /* id of parent task            */
 ompt_frame_t *parent_task_frame,  /* frame data of parent task    */
 ompt_parallel_id_t parallel_id,   /* id of parallel region        */
 uint32_t requested_team_size,     /* number of pregions in team   */
 void *parallel_function,           /* pointer to outlined function */
 char const *psource /*source to parallel region*/
)
{

#if DEBUG
  printf("begin parallel: parallel_id = %" PRIu64 ", "
	 "parent_task_frame %p, parent_task_id = %" PRIu64 " loc = %s \n", 
	 parallel_id, parent_task_frame, parent_task_id, psource);
#endif
  perf_profiler.CaptureParallelBegin(omp_get_thread_num());
  omp_profiler.captureParallelBegin(ompt_get_thread_id(), parent_task_id, parallel_id, requested_team_size, parallel_function , psource);
}

static void
on_ompt_event_parallel_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
 )
{
  omp_profiler.captureParallelEnd(ompt_get_thread_id(), parallel_id, task_id);
  perf_profiler.CaptureParallelEnd(omp_get_thread_num());
}

static void 
on_ompt_event_task_begin(ompt_task_id_t parent_task_id,    
                              ompt_frame_t *parent_task_frame,  
                              ompt_task_id_t new_task_id,       
                              void *new_task_function)
{
  omp_profiler.captureTaskBegin(ompt_get_thread_id(), ompt_get_parallel_id(0), parent_task_id, new_task_id, new_task_function);
  perf_profiler.CaptureTaskBegin(omp_get_thread_num());
}

static void
on_ompt_event_task_end(ompt_task_id_t  task_id)
{
  perf_profiler.CaptureTaskEnd(omp_get_thread_num());
  omp_profiler.captureTaskEnd( ompt_get_thread_id(), task_id);
}

static void
on_ompt_event_implicit_task_begin
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id,             /* id of task                  */
 char const *psource
)
{
  omp_profiler.captureImplicitTaskBegin(ompt_get_thread_id(), parallel_id, task_id, psource);
  perf_profiler.CaptureImplicitTaskBegin(omp_get_thread_num());
}


static void
on_ompt_event_implicit_task_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
)
{
  perf_profiler.CaptureImplicitTaskEnd(omp_get_thread_num());
  omp_profiler.captureImplicitTaskEnd(ompt_get_thread_id(), parallel_id, task_id);
}

static void
on_ompt_event_master_begin
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id,             /* id of task                  */
 char const *psource
)
{
  perf_profiler.CaptureMasterBegin(omp_get_thread_num());
  omp_profiler.captureMasterBegin(ompt_get_thread_id(), parallel_id, task_id, psource);
}


static void
on_ompt_event_master_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
)
{
  perf_profiler.CaptureMasterEnd(omp_get_thread_num());
  omp_profiler.captureMasterEnd(ompt_get_thread_id(), parallel_id, task_id);
}

static void
on_ompt_event_barrier_begin
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
)
{
  perf_profiler.CaptureBarrierBegin(omp_get_thread_num());
  omp_profiler.captureBarrierBegin(ompt_get_thread_id(), parallel_id, task_id);
}


static void
on_ompt_event_barrier_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
)
{
  omp_profiler.captureBarrierEnd(ompt_get_thread_id(), parallel_id, task_id);
  perf_profiler.CaptureBarrierEnd(omp_get_thread_num());
}

static void
on_ompt_event_taskwait_begin
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
)
{
  perf_profiler.CaptureTaskWaitBegin(omp_get_thread_num());
  omp_profiler.captureTaskwaitBegin(ompt_get_thread_id(), parallel_id, task_id); 
}

static void
on_ompt_event_taskwait_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
 ompt_task_id_t task_id             /* id of task                  */
)
{
  omp_profiler.captureTaskwaitEnd(ompt_get_thread_id(), parallel_id, task_id);
  perf_profiler.CaptureTaskWaitEnd(omp_get_thread_num()); 
}


static void on_ompt_event_task_alloc(ompt_task_id_t parent_task_id,    
                              ompt_frame_t *parent_task_frame,  
                              ompt_task_id_t new_task_id,       
                              void *new_task_function,
                              char const *psource)
{
  perf_profiler.CaptureTaskAllocEnter(omp_get_thread_num());
  omp_profiler.captureTaskAlloc(ompt_get_thread_id(), ompt_get_parallel_id(0), parent_task_id, new_task_id, new_task_function, psource);
  perf_profiler.CaptureTaskAllocExit(omp_get_thread_num());
}

static void on_ompt_event_single_in_block_begin
(ompt_parallel_id_t parallel_id, /* id of parallel region */
ompt_task_id_t parent_task_id, /* id of parent task */
void *workshare_function, /* pointer to outlined function */
char const *psource
){
  perf_profiler.CaptureSingleBeginEnter(omp_get_thread_num());
  omp_profiler.captureSingleBegin(ompt_get_thread_id(), parallel_id, parent_task_id, psource);
  perf_profiler.CaptureSingleBeginExit(omp_get_thread_num());
}


static void on_ompt_event_single_in_block_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
ompt_task_id_t task_id             /* id of task                  */
){
  perf_profiler.CaptureSingleEndEnter(omp_get_thread_num());
  omp_profiler.captureSingleEnd(ompt_get_thread_id(), parallel_id, task_id);
  perf_profiler.CaptureSingleEndExit(omp_get_thread_num());
}

static void on_ompt_event_single_others_begin
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
ompt_task_id_t task_id             /* id of task                  */
){
}

static void on_ompt_event_single_others_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
ompt_task_id_t task_id             /* id of task                  */
){
}

static void on_ompt_event_loop_begin
(ompt_parallel_id_t parallel_id, /* id of parallel region */
ompt_task_id_t parent_task_id, /* id of parent task */
void *workshare_function, /* pointer to outlined function */
char const *psource
){
  perf_profiler.CaptureLoopBeginEnter(omp_get_thread_num());
  omp_profiler.captureLoopBegin(ompt_get_thread_id(), parallel_id, parent_task_id, psource);
  perf_profiler.CaptureLoopBeginExit(omp_get_thread_num());
}


static void on_ompt_event_loop_end
(ompt_parallel_id_t parallel_id,    /* id of parallel region       */
ompt_task_id_t task_id             /* id of task                  */
){
#if DEBUG
  printf("[temp log] loop end: parallel_id = %" PRIu64 ", task_id = %" PRIu64 ", omp tid= %d" "\n",parallel_id, task_id, omp_get_thread_num());
#endif
  perf_profiler.CaptureLoopEndEnter(omp_get_thread_num());
  omp_profiler.captureLoopEnd(ompt_get_thread_id(), parallel_id, task_id);
  perf_profiler.CaptureLoopEndExit(omp_get_thread_num());

}

//critical
static void on_ompt_event_release_critical
(ompt_wait_id_t wait_id,
char const *psource)
{
#if DEBUG
  printf("[temp log] critical omp release loc %s, lock id = %" PRIu64 ", omp tid= %d\n", psource, wait_id, omp_get_thread_num());
#endif
  perf_profiler.CaptureReleaseCritical(omp_get_thread_num(), wait_id);
}

static void on_ompt_event_wait_critical
(ompt_wait_id_t wait_id,
char const *psource)
{
#if DEBUG
  printf("[temp log] critical omp wait loc %s, lock id = %" PRIu64 ", omp tid= %d\n", psource, wait_id, omp_get_thread_num());
#endif
  perf_profiler.CaptureWaitCritical(omp_get_thread_num(), wait_id);
}

static void on_ompt_event_acquired_critical
(ompt_wait_id_t wait_id,
char const *psource)
{
#if DEBUG
  printf("[temp log] critical omp acquired loc %s, lock id = %" PRIu64 ", omp tid= %d\n", psource, wait_id, omp_get_thread_num());
#endif
  perf_profiler.CaptureAcquiredCritical(omp_get_thread_num(), wait_id);
}

static void on_ompt_event_dispatch_next
(int lb,            /* lower bound                      */
int ub,            /* upper bound                      */
int st,            /* stride bound                     */
int last          /* last flag                        */
)
{
  //the chunk call happens before the chunk work is done
  perf_profiler.CaptureLoopChunkEnter(omp_get_thread_num());
  omp_profiler.captureLoopChunk(ompt_get_thread_id(), lb, ub, st, last);
  perf_profiler.CaptureLoopChunkExit(omp_get_thread_num());
#if DEBUG
  printf("[temp log] kmp dispatch tid= %d, lb=%d ub=%d st=%d last=%d\n", omp_get_thread_num(), lb, ub, st, last);
#endif
}

static void on_ompt_event_dispatch_next_others
(int last       /* last flag                        */
)
{
  perf_profiler.CaptureLoopChunkEnter(omp_get_thread_num());
  omp_profiler.captureLoopChunk(ompt_get_thread_id(), last);
  perf_profiler.CaptureLoopChunkExit(omp_get_thread_num());
  #if DEBUG
    printf("[temp log] kmp dispatch tid= %d, last=%d\n", omp_get_thread_num(), last);
  #endif
}
static void on_ompt_event_task_dependence_pair
(
  ompt_task_id_t src_task_id,
  ompt_task_id_t dst_task_id
)
{
}

static void on_ompt_event_task_dependences
(
  ompt_task_id_t task_id,            /* ID of task with dependences */
  const ompt_task_dependence_t *deps,/* vector of task dependences  */
  int ndeps                          /* number of dependences       */ 
)
{
  #if DEBUG
  printf("[ompt-tool log] task dependences task id = %" PRIu64 ", ndeps = %d\n", task_id, ndeps);
  for(int i=0; i< ndeps; i++){
    printf("[ompt-tool log] task id= = %" PRIu64 ", dep var= 0x%" PRIXPTR " type = %" PRIu32 "\n", 
    task_id, (uintptr_t)deps[i].variable_addr, deps[i].dependence_flags);
  }
  #endif
  omp_profiler.captureTaskDependences(ompt_get_thread_id(), task_id, deps, ndeps);
}
//*****************************************************************************
// interface operations 
//*****************************************************************************

void 
init_test(ompt_function_lookup_t lookup)
{
  if (!register_callback(ompt_event_thread_begin, 
      (ompt_callback_t) on_ompt_event_thread_begin)) {
    OMPP_CHECK(FALSE, FATAL, "failed to register ompt_event_thread_begin");
  }
  if (!register_callback(ompt_event_parallel_begin, 
			 (ompt_callback_t) on_ompt_event_parallel_begin)) {
    OMPP_CHECK(FALSE, FATAL, "failed to register ompt_event_parallel_begin");
  }
  if (!register_callback(ompt_event_parallel_end, 
			 (ompt_callback_t) on_ompt_event_parallel_end)) {
    OMPP_CHECK(FALSE, FATAL, "failed to register ompt_event_parallel_end");
  }
  if (!register_callback(ompt_event_task_begin, 
       (ompt_callback_t) on_ompt_event_task_begin)) {
    OMPP_CHECK(false, FATAL, "failed to register ompt_event_task_begin");
  }
  if (!register_callback(ompt_event_task_end, 
       (ompt_callback_t) on_ompt_event_task_end)) {
    OMPP_CHECK(false, FATAL, "failed to register ompt_event_task_begin");
  }
  if (!register_callback(ompt_event_implicit_task_begin, 
       (ompt_callback_t) on_ompt_event_implicit_task_begin)) {
    OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_implicit_task_begin");
  }
  if (!register_callback(ompt_event_implicit_task_end, 
       (ompt_callback_t) on_ompt_event_implicit_task_end)) {
    OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_implicit_task_end");
  }
  if (!register_callback(ompt_event_master_begin, 
       (ompt_callback_t) on_ompt_event_master_begin)) {
    OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_master_begin");
  }
  if (!register_callback(ompt_event_master_end, 
       (ompt_callback_t) on_ompt_event_master_end)) {
    OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_master_end");
  }
  if (!register_callback(ompt_event_barrier_begin, 
    (ompt_callback_t) on_ompt_event_barrier_begin)) {
    OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_barrier_begin");
  }
  if (!register_callback(ompt_event_barrier_end, 
      (ompt_callback_t) on_ompt_event_barrier_end)) {
    OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_barrier_end");
  }
  if (!register_callback(ompt_event_taskwait_begin, 
    (ompt_callback_t) on_ompt_event_taskwait_begin)) {
  OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_wait_taskwait_begin");
  }
  if (!register_callback(ompt_event_taskwait_end, 
    (ompt_callback_t) on_ompt_event_taskwait_end)) {
  OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_wait_taskwait_end");
  }
  if (!register_callback(ompt_event_task_alloc, 
    (ompt_callback_t) on_ompt_event_task_alloc)) {
  OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_task_alloc");
  }
  if (!register_callback(ompt_event_task_alloc, 
    (ompt_callback_t) on_ompt_event_task_alloc)) {
  OMPP_CHECK(false, FATAL, "failed to register on_ompt_event_task_alloc");
  }
  //single construct callbacks
  if (!register_callback(ompt_event_single_in_block_begin_wloc, 
    (ompt_callback_t) on_ompt_event_single_in_block_begin)) {
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_single_in_block_begin");
  }
  if (!register_callback(ompt_event_single_in_block_end, 
    (ompt_callback_t) on_ompt_event_single_in_block_end)) {
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_single_in_block_end");
  }
  if (!register_callback(ompt_event_single_others_begin, 
    (ompt_callback_t) on_ompt_event_single_others_begin)) {
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_single_others_begin");
  }
  if (!register_callback(ompt_event_single_others_end, 
    (ompt_callback_t) on_ompt_event_single_others_end)) {
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_single_others_end");
  }
  //loop callbacks
  if (!register_callback(ompt_event_loop_begin, 
    (ompt_callback_t) on_ompt_event_loop_begin)) {
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_loop_begin");
  }
  if (!register_callback(ompt_event_loop_end, 
    (ompt_callback_t) on_ompt_event_loop_end)) {
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_loop_end");
  }

  if (!register_callback(ompt_event_release_critical, 
    (ompt_callback_t) on_ompt_event_release_critical)) {
      printf("failed to register critical callback\n");
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_release_critical");
  }
  if (!register_callback(ompt_event_wait_critical, 
    (ompt_callback_t) on_ompt_event_wait_critical)) {
      printf("failed to register critical wait callback\n");
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_release_critical");
  }
  if (!register_callback(ompt_event_acquired_critical, 
    (ompt_callback_t) on_ompt_event_acquired_critical)) {
      printf("failed to register critical acquired callback\n");
  OMPP_CHECK(false, FATAL, "failed to register ompt_event_release_critical");
  }
  //dynamic loop
  if (!register_callback(ompt_event_dispatch_next, 
    (ompt_callback_t) on_ompt_event_dispatch_next)) {
      printf("failed to register dispatch callback\n");
    OMPP_CHECK(false, FATAL, "failed to register ompt_event_dispatch_next");
  }
  if (!register_callback(ompt_event_dispatch_next_others, 
    (ompt_callback_t) on_ompt_event_dispatch_next_others)) {
      printf("failed to register dispatch others callback\n");
    OMPP_CHECK(false, FATAL, "failed to register ompt_event_dispatch_next_others");
  }
  //task dependence
  if (!register_callback(ompt_event_task_dependence_pair, 
    (ompt_callback_t) on_ompt_event_task_dependence_pair)) {
      printf("failed to register task dependence pair callback\n");
    OMPP_CHECK(false, FATAL, "failed to register task dependence pair callback");
  }
  if (!register_callback(ompt_event_task_dependences, 
    (ompt_callback_t) on_ompt_event_task_dependences)) {
      printf("failed to register task dependences callback\n");
    OMPP_CHECK(false, FATAL, "failed to register task dependences callback");
  }

  
} 

void ompprof_init(){
  // force initialization of the openmp runtime system
  omp_get_max_threads();
  
  OMPP_CHECK(ompp_ompt_initialized, FATAL, 
        "no call to ompt_initialize. test aborted."); 

  
  serial_task_id = ompt_get_task_id(0);
  serial_task_frame = ompt_get_task_frame(0);
  ompt_parallel_id_t serial_parallel_id = ompt_get_parallel_id(0);
  ompt_thread_id_t serial_thread_id = ompt_get_thread_id();
  omp_profiler.initProfiler(serial_thread_id, serial_parallel_id, serial_task_id);
  perf_profiler.InitProfiler();
#if DEBUG
  printf("serial_task_id = %" PRIu64 ", serial_task_frame = %p, thread_id = %" PRIu64 "\n", 
	 serial_task_id, serial_task_frame, serial_thread_id);
#endif
}

void ompprof_fini(){
  perf_profiler.finishProfile();
  omp_profiler.finishProfile();
}
