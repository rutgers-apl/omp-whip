//*****************************************************************************
// system include files 
//*****************************************************************************

#include <stdio.h>
#include <signal.h>

extern int openmp_init();

//*****************************************************************************
// local include files 
//*****************************************************************************

#include "ompt-regression.h"
#include "ompt-openmp.h"



//*****************************************************************************
// global variables
//*****************************************************************************
    
//pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t ompp_assert_mutex = PTHREAD_MUTEX_INITIALIZER; 

int ompp_return_code = CORRECT;
const char *ompp_regression_test_name = "";
int ompp_ompt_initialized = 0;


volatile double ompp_a = 0.5, ompp_b = 2.2;

void dummy( void *array )
{
/* Confuse the compiler so as not to optimize
   away the flops in the calling routine    */
/* Cast the array as a void to eliminate unused argument warning */
	( void ) array;
}

void serialwork( int n )
{
	int i;
	double c = 0.11;

	for ( i = 0; i < n; i++ ) {
		c += ompp_a * ompp_b;
	}
	dummy( ( void * ) &c );
}
