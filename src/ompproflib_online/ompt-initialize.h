#ifndef ompt_initialize_h
#define	ompt_initialize_h


//*****************************************************************************
// OpenMP includes
//*****************************************************************************
#include <ompt.h>



//*****************************************************************************
// global variables
//*****************************************************************************

//-----------------------------------------------------------------------------
// OMPT function pointers 
//-----------------------------------------------------------------------------
#define macro( fn ) extern fn ## _t fn;
FOREACH_OMPT_INQUIRY_FN( macro )
#undef macro


#if defined(__cplusplus)
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Function: quit_on_init_failure
// 
// Purpose: exit if initialization fails. this function is called in part to
//          force this file to be linked in so that the regression test version
//          of  ompt_initialize is included in the executable. it wouldn't be
//          otherwise if register_callback is not called, which is the 
//          case for inquiry tests. 
//-----------------------------------------------------------------------------
void quit_on_init_failure();

//-----------------------------------------------------------------------------
// Function: register_callback
// 
// Purpose: simple interface to register OMPT callbacks 
//-----------------------------------------------------------------------------
int register_callback(ompt_event_t e, ompt_callback_t c);


//-----------------------------------------------------------------------------
// Function: init_test
// 
// Purpose: callback from an implementation of ompt_initialize in the 
//          regression harness that can be used to register OMPT callbacks
//-----------------------------------------------------------------------------
extern void init_test(ompt_function_lookup_t lookup);


#if defined(__cplusplus)
};
#endif

#endif

