
//*****************************************************************************
// system includes
//*****************************************************************************

#include <map>
#include <set>
#include <inttypes.h>
#include "openmp_profiler.h"
#include "perf_profiler.h"
#include "causalProfiler.h"

//*****************************************************************************
// OpenMP runtime includes
//*****************************************************************************
#include <omp.h>
#include <ompt.h>

//to detect system arch
#include "kmp_platform.h"

//*****************************************************************************
// macros
//*****************************************************************************

#define DEBUG 0

static const char* ompt_thread_type_t_values[] = {
  NULL,
  "ompt_thread_initial",//1
  "ompt_thread_worker",
  "ompt_thread_other"
};

static const char* ompt_task_status_t_values[] = {
  NULL,
  "ompt_task_complete",
  "ompt_task_yield",
  "ompt_task_cancel",
  "ompt_task_others"
};
static const char* ompt_cancel_flag_t_values[] = {
  "ompt_cancel_parallel",
  "ompt_cancel_sections",
  "ompt_cancel_do",
  "ompt_cancel_taskgroup",
  "ompt_cancel_activated",
  "ompt_cancel_detected",
  "ompt_cancel_discarded_task"
};

static const char* ompt_task_dependence_flag_t_values[] = {
  NULL,
  "ompt_task_dependence_type_out",
  "ompt_task_dependence_type_in",
  "ompt_task_dependence_type_inout"
};


static ompt_set_callback_t ompt_set_callback;
static ompt_get_task_info_t ompt_get_task_info;
static ompt_get_thread_data_t ompt_get_thread_data;
static ompt_get_parallel_info_t ompt_get_parallel_info;
static ompt_get_unique_id_t ompt_get_unique_id;
static ompt_get_num_procs_t ompt_get_num_procs;
static ompt_get_num_places_t ompt_get_num_places;
static ompt_get_place_proc_ids_t ompt_get_place_proc_ids;
static ompt_get_place_num_t ompt_get_place_num;
static ompt_get_partition_place_nums_t ompt_get_partition_place_nums;
static ompt_get_proc_id_t ompt_get_proc_id;
static ompt_enumerate_states_t ompt_enumerate_states;
static ompt_enumerate_mutex_impls_t ompt_enumerate_mutex_impls;

//*****************************************************************************
// ompt utility functions
//*****************************************************************************

static void print_ids(int level)
{
  ompt_frame_t* frame ;
  ompt_data_t* parallel_data;
  ompt_data_t* task_data;
  int exists_task = ompt_get_task_info(level, NULL, &task_data, &frame, &parallel_data, NULL);
  if (frame)
  {
    printf("%" PRIu64 ": task level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", exit_frame=%p, reenter_frame=%p\n", ompt_get_thread_data()->value, level, exists_task ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame->exit_frame, frame->enter_frame);
  }
  else
    printf("%" PRIu64 ": task level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", frame=%p\n", ompt_get_thread_data()->value, level, exists_task ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame);
}

#define print_frame(level)\
do {\
  printf("%" PRIu64 ": __builtin_frame_address(%d)=%p\n", ompt_get_thread_data()->value, level, __builtin_frame_address(level));\
} while(0)

// clang (version 5.0 and above) adds an intermediate function call with debug flag (-g)
#if defined(TEST_NEED_PRINT_FRAME_FROM_OUTLINED_FN)
  #if defined(DEBUG) && defined(__clang__) && __clang_major__ >= 5
    #define print_frame_from_outlined_fn(level) print_frame(level+1)
  #else
    #define print_frame_from_outlined_fn(level) print_frame(level)
  #endif

  #warning "Clang 5.0 and later add an additional wrapper function for tasks when compiling with debug information."
  #warning "Please define -DDEBUG iff you manually pass in -g!"
#endif

// This macro helps to define a label at the current position that can be used
// to get the current address in the code.
//
// For print_current_address():
//   To reliably determine the offset between the address of the label and the
//   actual return address, we insert a NOP instruction as a jump target as the
//   compiler would otherwise insert an instruction that we can't control. The
//   instruction length is target dependent and is explained below.
//
// (The empty block between "#pragma omp ..." and the __asm__ statement is a
// workaround for a bug in the Intel Compiler.)
#define define_ompt_label(id) \
  {} \
  __asm__("nop"); \
ompt_label_##id:

// This macro helps to get the address of a label that is inserted by the above
// macro define_ompt_label(). The address is obtained with a GNU extension
// (&&label) that has been tested with gcc, clang and icc.
#define get_ompt_label_address(id) (&& ompt_label_##id)

// This macro prints the exact address that a previously called runtime function
// returns to.
#define print_current_address(id) \
  define_ompt_label(id) \
  print_possible_return_addresses(get_ompt_label_address(id))

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// On X86 the NOP instruction is 1 byte long. In addition, the comiler inserts
// a MOV instruction for non-void runtime functions which is 3 bytes long.
#define print_possible_return_addresses(addr) \
  printf("%" PRIu64 ": current_address=%p or %p for non-void functions\n", \
         ompt_get_thread_data()->value, ((char *)addr) - 1, ((char *)addr) - 4)
#elif KMP_ARCH_PPC64
// On Power the NOP instruction is 4 bytes long. In addition, the compiler
// inserts an LD instruction which accounts for another 4 bytes. In contrast to
// X86 this instruction is always there, even for void runtime functions.
#define print_possible_return_addresses(addr) \
  printf("%" PRIu64 ": current_address=%p\n", ompt_get_thread_data()->value, \
         ((char *)addr) - 8)
#elif KMP_ARCH_AARCH64
// On AArch64 the NOP instruction is 4 bytes long, can be followed by inserted
// store instruction (another 4 bytes long).
#define print_possible_return_addresses(addr) \
  printf("%" PRIu64 ": current_address=%p or %p\n", ompt_get_thread_data()->value, \
         ((char *)addr) - 4, ((char *)addr) - 8)
#else
#error Unsupported target architecture, cannot determine address offset!
#endif


// This macro performs a somewhat similar job to print_current_address(), except
// that it discards a certain number of nibbles from the address and only prints
// the most significant bits / nibbles. This can be used for cases where the
// return address can only be approximated.
//
// To account for overflows (ie the most significant bits / nibbles have just
// changed as we are a few bytes above the relevant power of two) the addresses
// of the "current" and of the "previous block" are printed.
#define print_fuzzy_address(id) \
  define_ompt_label(id) \
  print_fuzzy_address_blocks(get_ompt_label_address(id))

// If you change this define you need to adapt all capture patterns in the tests
// to include or discard the new number of nibbles!
#define FUZZY_ADDRESS_DISCARD_NIBBLES 2
#define FUZZY_ADDRESS_DISCARD_BYTES (1 << ((FUZZY_ADDRESS_DISCARD_NIBBLES) * 4))
#define print_fuzzy_address_blocks(addr) \
  printf("%" PRIu64 ": fuzzy_address=0x%" PRIx64 " or 0x%" PRIx64 " (%p)\n", \
  ompt_get_thread_data()->value, \
  ((uint64_t)addr) / FUZZY_ADDRESS_DISCARD_BYTES - 1, \
  ((uint64_t)addr) / FUZZY_ADDRESS_DISCARD_BYTES, addr)


static void format_task_type(int type, char* buffer)
{
  char* progress = buffer;
  if(type & ompt_task_initial) progress += sprintf(progress, "ompt_task_initial");
  if(type & ompt_task_implicit) progress += sprintf(progress, "ompt_task_implicit");
  if(type & ompt_task_explicit) progress += sprintf(progress, "ompt_task_explicit");
  if(type & ompt_task_target) progress += sprintf(progress, "ompt_task_target");
  if(type & ompt_task_undeferred) progress += sprintf(progress, "|ompt_task_undeferred");
  if(type & ompt_task_untied) progress += sprintf(progress, "|ompt_task_untied");
  if(type & ompt_task_final) progress += sprintf(progress, "|ompt_task_final");
  if(type & ompt_task_mergeable) progress += sprintf(progress, "|ompt_task_mergeable");
  if(type & ompt_task_merged) progress += sprintf(progress, "|ompt_task_merged");
}

//*****************************************************************************
// private operations 
//*****************************************************************************

//utility functions

/** @brief used to generate OpenMP thread ids starting from 0
 *  @return void
 */
static uint64_t omw_next_tid()
{
  static uint64_t THREAD_ID=0;
  uint64_t ret = __sync_fetch_and_add(&THREAD_ID,1);
  return ret;
}

/** @brief used to generate OpenMP parallel ids starting from 1
 *  @return void
 */
static uint64_t omw_next_parallel_id()
{
  static uint64_t PARALLEL_ID=1;
  uint64_t ret = __sync_fetch_and_add(&PARALLEL_ID,1);
  return ret;
}

/** @brief used to generate OpenMP  task ids starting from 1
 *  @return void
 */
static uint64_t omw_next_task_id()
{
  static uint64_t TASK_ID=1;
  uint64_t ret = __sync_fetch_and_add(&TASK_ID,1);
  return ret;
}

/** @brief callback called when a new thread gets created by the openmp runtime
 *  @param thread_type indicates the type of thread created intial/worker/other/unknown
 *  @param thread_data pointer to the associated thread data. gets set here
 *  @return void
 */
static void
on_ompt_callback_thread_begin(
  ompt_thread_type_t thread_type,
  ompt_data_t *thread_data)
{
  if(thread_data->ptr)
    printf("%s\n", "0: thread_data initially not null");
//  thread_data->value = omw_next_tid();
  thread_data->value = omp_get_thread_num();

  if (thread_type == ompt_thread_type_t::ompt_thread_initial){
    //initial thread start
  }
  printf("%" PRIu64 ": ompt_event_thread_begin: thread_type=%s=%d, thread_id=%" PRIu64 
  "\n", ompt_get_thread_data()->value, ompt_thread_type_t_values[thread_type], 
  thread_type, thread_data->value);

  omp_profiler.captureThreadBegin(thread_data->value);
}

/** @brief called when a thread ends. was not used in ompprof llvm 5.0 version
 *  @param thread_data pointer to associated thread data set up during thread_begin event
 *  @return void
 */ 
static void
on_ompt_callback_thread_end(
  ompt_data_t *thread_data)
{
  //#if DEBUG
  printf("%" PRIu64 ": ompt_event_thread_end: thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data->value);
  //#endif
}

/** @brief called when a task reaches a parallel directive
 *  @param encountering_task_data task data associated with the task reaching
 *  parallel directive. I think I need to set it up with data in task create
 *  @param psource is the parallel directive's source code information 
 *  @return void
 */
static void
on_ompt_callback_parallel_begin(
  ompt_data_t *encountering_task_data,
  const ompt_frame_t *encountering_task_frame,
  ompt_data_t* parallel_data,
  uint32_t requested_team_size,
  ompt_invoker_t invoker,
  const void *codeptr_ra,
  char const *psource)
{
  if(parallel_data->ptr)
    printf("0: parallel_data initially not null\n");
  parallel_data->value = omw_next_parallel_id();
  //parallel_data->value = ompt_get_unique_id();
  uint64_t tid = ompt_get_thread_data()->value;
  printf("%" PRIu64 ": ompt_event_parallel_begin: parent_task_id=%" PRIu64 
  ", parent_task_frame.exit=%p, parent_task_frame.reenter=%p, parallel_id=%" PRIu64 
  ", requested_team_size=%" PRIu32 ", codeptr_ra=%p, invoker=%d, loc=%s\n", 
  ompt_get_thread_data()->value, encountering_task_data->value, 
  encountering_task_frame->exit_frame, encountering_task_frame->enter_frame, 
  parallel_data->value, requested_team_size, codeptr_ra, invoker, psource);
  //print_current_address(0);
  perf_profiler.CaptureParallelBegin(tid);
  omp_profiler.captureParallelBegin(tid, encountering_task_data->value, parallel_data->value, 
  requested_team_size, psource);
}

static void
on_ompt_callback_parallel_end(
  ompt_data_t *parallel_data,
  ompt_data_t *encountering_task_data,
  ompt_invoker_t invoker,
  const void *codeptr_ra)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  printf("%" PRIu64 ": ompt_event_parallel_end: parallel_id=%" PRIu64 
  ", task_id=%" PRIu64 ", invoker=%d, codeptr_ra=%p\n", 
  current_tid, parallel_data->value, 
  encountering_task_data->value, invoker, codeptr_ra);

  ompt_data_t* task_data;
  int exists_task = ompt_get_task_info(0, NULL, &task_data, NULL, NULL, NULL);
  omp_profiler.captureParallelEnd(current_tid,parallel_data->value, exists_task ? task_data->value : 0);
  perf_profiler.CaptureParallelEnd(current_tid);
}

/**
 * TODO add psource
 */
static void
on_ompt_callback_task_create(
    ompt_data_t *encountering_task_data,
    const ompt_frame_t *encountering_task_frame,
    ompt_data_t* new_task_data,
    int type,
    int has_dependences,
    const void *codeptr_ra)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  if(new_task_data->ptr)
    printf("0: new_task_data initially not null\n");
  //new_task_data->value = ompt_get_unique_id();
  new_task_data->value = omw_next_task_id();
  char buffer[2048];

  format_task_type(type, buffer);

  //there is no parallel_begin callback for implicit parallel region
  //thus it is initialized in initial task
  if(type & ompt_task_initial)
  {
    ompt_data_t *parallel_data;
    ompt_get_parallel_info(0, &parallel_data, NULL);
    if(parallel_data->ptr)//maybe add an assertion here?
      printf("%s\n", "0: parallel_data initially not null");
    parallel_data->value = omw_next_parallel_id();
  }
  //check for explicit task here and call task_alloc
  else if(type & ompt_task_explicit){
    //perf_profiler.CaptureTaskAllocEnter(current_tid);
    //parallel_id and parent_task_id are only used for logging purposes
    //omp_profiler.captureTaskAlloc(current_tid, ompt_get_parallel_id(0), encountering_task_data ? encountering_task_data->value : 0, 
    //new_task_data->value, NULL, "psource");
    //perf_profiler.CaptureTaskAllocExit(current_tid);
  }
  
  printf("%" PRIu64 ": ompt_event_task_create: parent_task_id=%" PRIu64 
  ", parent_task_frame.exit=%p, parent_task_frame.reenter=%p, new_task_id=%" PRIu64 
  ", codeptr_ra=%p, task_type=%s=%d, has_dependences=%s\n", 
  ompt_get_thread_data()->value, encountering_task_data ? encountering_task_data->value : 0, 
  encountering_task_frame ? encountering_task_frame->exit_frame : NULL, 
  encountering_task_frame ? encountering_task_frame->enter_frame : NULL, 
  new_task_data->value, codeptr_ra, buffer, type, has_dependences ? "yes" : "no");
}

static void
on_ompt_callback_task_schedule(
    ompt_data_t *first_task_data,
    ompt_task_status_t prior_task_status,
    ompt_data_t *second_task_data)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  printf("%" PRIu64 ": ompt_event_task_schedule: first_task_id=%" PRIu64 
  ", second_task_id=%" PRIu64 ", prior_task_status=%s=%d\n", 
  ompt_get_thread_data()->value, first_task_data->value, second_task_data->value, ompt_task_status_t_values[prior_task_status], prior_task_status);
  //this works if explicit tasks are always tied as is the case with all benchmarks we could find
  //check to refine the exact condition
  //pass second task data to omp_profiler.captureTaskBegin but let it return if taskmap does not include the taskId
  //should should not occur and there is an assertion for this in omp_profiler.captureTaskBegin
  ompt_data_t *parallel_data;
  ompt_get_parallel_info(0, &parallel_data, NULL);
  //parent/prior task is only used for logging purposes
  //omp_profiler.captureTaskBegin(current_tid, parallel_data ? parallel_data->value : 0, 
  //                              first_task_data->value, second_task_data->value);
  //perf_profiler.CaptureTaskBegin(current_tid);

  if(prior_task_status == ompt_task_complete)
  {
    printf("%" PRIu64 ": ompt_event_task_end: task_id=%" PRIu64 
    "\n", current_tid, first_task_data->value);

    //perf_profiler.CaptureTaskEnd(current_tid);
    //omp_profiler.captureTaskEnd(current_tid, first_task_data->value);
  }
}

static void
on_ompt_callback_task_dependences(
  ompt_data_t *task_data,
  const ompt_task_dependence_t *deps,
  int ndeps)
{
  uint64_t current_tid = ompt_get_thread_data()->value;

  #if DEBUG
  printf("%" PRIu64 ": ompt_event_task_dependences: task_id=%" PRIu64 
  ", deps=%p, ndeps=%d\n", current_tid, task_data->value, (void *)deps, ndeps);
  for(int i=0; i< ndeps; i++){
    printf("task_id= = %" PRIu64 ", dep var= 0x%" PRIXPTR " type=%s %" PRIu32 "\n", 
    task_data->value, (uintptr_t)deps[i].variable_addr, 
    ompt_task_dependence_flag_t_values[deps[i].dependence_flags], deps[i].dependence_flags);
  }
  #endif
  omp_profiler.captureTaskDependences(current_tid, task_data->value, deps, ndeps);
}

static void
on_ompt_callback_task_dependence(
  ompt_data_t *first_task_data,
  ompt_data_t *second_task_data)
{
  //printf("%" PRIu64 ": ompt_event_task_dependence_pair: first_task_id=%" PRIu64 ", second_task_id=%" PRIu64 "\n", ompt_get_thread_data()->value, first_task_data->value, second_task_data->value);
}


/** @brief called when a task reaches a synchronization construct including 
 * implicit/explicit barriers.
 *  @param kind determines the synchronization construct encountered.
 *  parallel directive. I think I need to set it up with data in task create
 *  @param endpoint denotes if its the beginning or the end of the construct
 *  @return void
 */
static void
on_ompt_callback_sync_region(
  ompt_sync_region_kind_t kind,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  const void *codeptr_ra)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  switch(endpoint)
  {
    case ompt_scope_begin:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          printf("%" PRIu64 ": ompt_event_barrier_begin: parallel_id=%" PRIu64 
          ", task_id=%" PRIu64 ", codeptr_ra=%p\n", 
          ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);

          perf_profiler.CaptureBarrierBegin(current_tid);
          omp_profiler.captureBarrierBegin(current_tid, parallel_data->value, task_data->value);
          //print_ids(0);
          break;
        case ompt_sync_region_taskwait:
          printf("%" PRIu64 ": ompt_event_taskwait_begin: parallel_id=%" PRIu64 
          ", task_id=%" PRIu64 ", codeptr_ra=%p\n", 
          ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);

          perf_profiler.CaptureTaskWaitBegin(current_tid);
          omp_profiler.captureTaskwaitBegin(current_tid, parallel_data->value, task_data->value);
          break;
        case ompt_sync_region_taskgroup:
          printf("%" PRIu64 ": ompt_event_taskgroup_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
          break;
      }
      break;
    case ompt_scope_end:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          printf("%" PRIu64 ": ompt_event_barrier_end: parallel_id=%" PRIu64 
          ", task_id=%" PRIu64 ", codeptr_ra=%p\n", 
          ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);

          omp_profiler.captureBarrierEnd(current_tid, (parallel_data)?parallel_data->value:0, task_data->value);
          //this order since perf should start count after profling is done
          perf_profiler.CaptureBarrierEnd(current_tid);
          break;
        case ompt_sync_region_taskwait:
          printf("%" PRIu64 ": ompt_event_taskwait_end: parallel_id=%" PRIu64 
          ", task_id=%" PRIu64 ", codeptr_ra=%p\n", 
          ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);

          omp_profiler.captureTaskwaitEnd(current_tid, (parallel_data)?parallel_data->value:0, task_data->value);
          perf_profiler.CaptureTaskWaitEnd(current_tid);
          break;
        case ompt_sync_region_taskgroup:
          printf("%" PRIu64 ": ompt_event_taskgroup_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);
          break;
      }
      break;
  }
}


static void
on_ompt_callback_sync_region_wait(
  ompt_sync_region_kind_t kind,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  const void *codeptr_ra)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  switch(endpoint)
  {
    case ompt_scope_begin:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          printf("%" PRIu64 ": ompt_event_wait_barrier_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
	//TODO temp
	// perf_profiler.CaptureImplicitTaskEnd(current_tid);
      	//omp_profiler.captureImplicitTaskEnd(current_tid, parallel_data->value, task_data->value);

          break;
        case ompt_sync_region_taskwait:
          printf("%" PRIu64 ": ompt_event_wait_taskwait_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
          break;
        case ompt_sync_region_taskgroup:
          printf("%" PRIu64 ": ompt_event_wait_taskgroup_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
          break;
      }
      break;
    case ompt_scope_end:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          printf("%" PRIu64 ": ompt_event_wait_barrier_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);
          break;
        case ompt_sync_region_taskwait:
          printf("%" PRIu64 ": ompt_event_wait_taskwait_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);
          break;
        case ompt_sync_region_taskgroup:
          printf("%" PRIu64 ": ompt_event_wait_taskgroup_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);
          break;
      }
      break;
  }
}

/** @brief called when and implicit-barrier-begin or implicit-barrier-end occurs
 *  @param endpoint denotes whether its a barrier-begin/end
 *  @param parallel_data is the data associated with the current parallel region. its 0 for implicit-barrier-end event
 *  @param task_data is the data associated with the current implicit task. needs to be set up in begin
 *  @param team_size is the size of the current team
 *  @param thread_num is the thread id of the thread running the implicit task within the current time
 *  TODO add psource
 */
static void
on_ompt_callback_implicit_task(
    ompt_scope_endpoint_t endpoint,
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    unsigned int team_size,
    unsigned int thread_num)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  switch(endpoint)
  {
    case ompt_scope_begin:
      if(task_data->ptr)
        printf("%s\n", "0: task_data initially not null");
      //task_data->value = ompt_get_unique_id();
      task_data->value = omw_next_task_id();
      //cannot use thread_num as it doesn't match the thread_ids set during thread_begin
      
      omp_profiler.captureImplicitTaskBegin(current_tid, parallel_data->value, task_data->value, "psource");
      perf_profiler.CaptureImplicitTaskBegin(current_tid);
      
      printf("%" PRIu64 ": ompt_event_implicit_task_begin: parallel_id=%" PRIu64 
      ", task_id=%" PRIu64 ", team_size=%" PRIu32 ", thread_num=%" PRIu32 "\n", 
      ompt_get_thread_data()->value, parallel_data->value, task_data->value, team_size, thread_num);
      break;
    case ompt_scope_end:
      perf_profiler.CaptureImplicitTaskEnd(current_tid);
      //since no new node gets created, the second and third argument are not being used except for logging purposes
      //omp_profiler.captureImplicitTaskEnd(current_tid, 0, task_data->value);
      omp_profiler.captureImplicitTaskEnd(current_tid, (parallel_data)?parallel_data->value:0, task_data->value);
      printf("%" PRIu64 ": ompt_event_implicit_task_end: parallel_id=%" PRIu64 
      ", task_id=%" PRIu64 ", team_size=%" PRIu32 ", thread_num=%" PRIu32 "\n", 
      ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, team_size, thread_num);
      break;
  }
}

/** todo add psource in all occurrences of the call
 * 
 */
static void
on_ompt_callback_work(
  ompt_work_type_t wstype,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  uint64_t count,
  const void *codeptr_ra)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  switch(endpoint)
  {
    case ompt_scope_begin:
      switch(wstype)
      {
        case ompt_work_loop:
          printf("%" PRIu64 ": ompt_event_loop_begin: parallel_id=%" PRIu64 
          ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", 
          ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);

          perf_profiler.CaptureLoopBeginEnter(current_tid);
          omp_profiler.captureLoopBegin(current_tid, parallel_data->value, task_data->value, "psource");
          perf_profiler.CaptureLoopBeginExit(current_tid);
          break;
        case ompt_work_sections:
          printf("%" PRIu64 ": ompt_event_sections_begin: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        case ompt_work_single_executor:
          printf("%" PRIu64 ": ompt_event_single_in_block_begin: parallel_id=%" PRIu64 
          ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", 
          ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);

          //perf_profiler.CaptureSingleBeginEnter(current_tid);
          //omp_profiler.captureSingleBegin(current_tid, parallel_data->value, task_data->value, "psource");
          //perf_profiler.CaptureSingleBeginExit(current_tid);
          break;
        //no need to handle unless we want to stop counters and generate two work nodes
        case ompt_work_single_other:
          printf("%" PRIu64 ": ompt_event_single_others_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        //this cosntruct is fortran only 
        case ompt_work_workshare:
          break;
        case ompt_work_distribute:
          printf("%" PRIu64 ": ompt_event_distribute_begin: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        //todo
        case ompt_work_taskloop:
          printf("%" PRIu64 ": ompt_event_taskloop_begin: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
      }
      break;
    case ompt_scope_end:
      switch(wstype)
      {
        case ompt_work_loop:
          printf("%" PRIu64 ": ompt_event_loop_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        case ompt_work_sections:
          printf("%" PRIu64 ": ompt_event_sections_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        case ompt_work_single_executor:
          printf("%" PRIu64 ": ompt_event_single_in_block_end: parallel_id=%" PRIu64 
          ", task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", 
          ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);

          //perf_profiler.CaptureSingleEndEnter(current_tid);
          //omp_profiler.captureSingleEnd(current_tid, parallel_data->value, task_data->value, "psource");
          //perf_profiler.CaptureSingleEndExit(current_tid);
          break;
        //no need to handle unless we want to stop counters and generate two work nodes 
        case ompt_work_single_other:
          printf("%" PRIu64 ": ompt_event_single_others_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        //this construct is fortran only
        case ompt_work_workshare:
          break;
        case ompt_work_distribute:
          printf("%" PRIu64 ": ompt_event_distribute_end: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
        //todo
        case ompt_work_taskloop:
          printf("%" PRIu64 ": ompt_event_taskloop_end: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", codeptr_ra=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
          break;
      }
      break;
  }
}

/**
 * TODO add psource
 */
static void
on_ompt_callback_master(
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  const void *codeptr_ra)
{
  uint64_t current_tid = ompt_get_thread_data()->value;
  switch(endpoint)
  {
    case ompt_scope_begin:
      printf("%" PRIu64 ": ompt_event_master_begin: parallel_id=%" PRIu64 
      ", task_id=%" PRIu64 ", codeptr_ra=%p\n", current_tid, 
      parallel_data->value, task_data->value, codeptr_ra);
      
      //perf_profiler.CaptureMasterBegin(current_tid);
      //omp_profiler.captureMasterBegin(current_tid, parallel_data->value, task_data->value, "psource");
      break;
    case ompt_scope_end:
      printf("%" PRIu64 ": ompt_event_master_end: parallel_id=%" PRIu64 
      ", task_id=%" PRIu64 ", codeptr_ra=%p\n", current_tid, 
      parallel_data->value, task_data->value, codeptr_ra);
      
      //perf_profiler.CaptureMasterEnd(current_tid);
      //omp_profiler.captureMasterEnd(ompt_get_thread_id(), parallel_data->value, task_data->value);
      break;
  }
}

#define register_callback_t(name, type)                       \
do{                                                           \
  type f_##name = &on_##name;                                 \
  if (ompt_set_callback(name, (ompt_callback_t)f_##name) ==   \
      ompt_set_never)                                         \
    printf("0: Could not register callback '" #name "'\n");   \
}while(0)

#define register_callback(name) register_callback_t(name, name##_t)

void ompprof_init(){
  // force initialization of the openmp runtime system
  printf("ompprof_init start\n");
  omp_get_max_threads();
  
  uint64_t serial_thread_id = ompt_get_thread_data()->value;
  
  ompt_data_t* task_data;
  int exists_task = ompt_get_task_info(0, NULL, &task_data, NULL, NULL, NULL);
  //at this point there is no parallel region
  uint64_t serial_parallel_id = omw_next_parallel_id();
  //uint64_t serial_parallel_id = parallel_data->value;
  //however there is a task_data associated with the task_create for the initial master task
  uint64_t serial_task_id = task_data->value;
  //uint64_t serial_task_id = omw_next_task_id();

  omp_profiler.initProfiler(serial_thread_id, serial_parallel_id, serial_task_id);
  perf_profiler.InitProfiler();
// #if DEBUG
//printf("[debug] serial_thread_id = %" PRIu64 ", serial_parallel_id = %" PRIu64 ", serial_task_id = %" PRIu64 "\n", 
//serial_thread_id,serial_parallel_id,serial_task_id);
// #endif
}

void ompprof_fini(){
  printf("ompprof_fini start\n");
//  omp_profiler.syncEnd();
  perf_profiler.finishProfile();
  omp_profiler.finishProfile();
}

void ompprof_sync(){
  omp_profiler.captureParallelEnd(0,1,1);
}
int ompt_initialize(
  ompt_function_lookup_t lookup,
  ompt_data_t* data)
{

  printf("ompt_initialize\n");
  ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
  ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");
  ompt_get_thread_data = (ompt_get_thread_data_t) lookup("ompt_get_thread_data");
  ompt_get_parallel_info = (ompt_get_parallel_info_t) lookup("ompt_get_parallel_info");
  ompt_get_unique_id = (ompt_get_unique_id_t) lookup("ompt_get_unique_id");

  ompt_get_num_procs = (ompt_get_num_procs_t) lookup("ompt_get_num_procs");
  ompt_get_num_places = (ompt_get_num_places_t) lookup("ompt_get_num_places");
  ompt_get_place_proc_ids = (ompt_get_place_proc_ids_t) lookup("ompt_get_place_proc_ids");
  ompt_get_place_num = (ompt_get_place_num_t) lookup("ompt_get_place_num");
  ompt_get_partition_place_nums = (ompt_get_partition_place_nums_t) lookup("ompt_get_partition_place_nums");
  ompt_get_proc_id = (ompt_get_proc_id_t) lookup("ompt_get_proc_id");
  ompt_enumerate_states = (ompt_enumerate_states_t) lookup("ompt_enumerate_states");
  ompt_enumerate_mutex_impls = (ompt_enumerate_mutex_impls_t) lookup("ompt_enumerate_mutex_impls");

  
  register_callback(ompt_callback_sync_region);
  //don't use barrier wait begin and exit for now
  register_callback_t(ompt_callback_sync_region_wait, ompt_callback_sync_region_t);
  register_callback(ompt_callback_implicit_task);
  
  register_callback(ompt_callback_work);
  register_callback(ompt_callback_master);
  register_callback_t(ompt_callback_parallel_begin, ompt_callback_parallel_begin_wloc_t);
  register_callback(ompt_callback_parallel_end);
  register_callback(ompt_callback_task_create);
  register_callback(ompt_callback_task_schedule);
  register_callback(ompt_callback_task_dependences);
  register_callback(ompt_callback_task_dependence);
  register_callback(ompt_callback_thread_begin);
  register_callback(ompt_callback_thread_end);
  //printf("0: NULL_POINTER=%p\n", (void*)NULL);

  return 1; //success
}

void ompt_finalize(ompt_data_t* data)
{
  printf("ompt_finalize\n");
 // perf_profiler.finishProfile();
 // omp_profiler.finishProfile();

}


ompt_start_tool_result_t* ompt_start_tool(
  unsigned int omp_version,
  const char *runtime_version)
{
  static ompt_start_tool_result_t ompt_start_tool_result = {&ompt_initialize,&ompt_finalize, 0};
  return &ompt_start_tool_result;
}
