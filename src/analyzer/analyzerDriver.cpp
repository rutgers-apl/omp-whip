#include <iostream>
#include <string.h>
#include <sstream>
#include <fstream>

#include "ATreeNode.h"
#include "profileReader.h"

//0 no lock analysis
//1 lock analysis
//TODO use input argument parsing rather than a global var
int mode = 0;

int main(int argc, char* argv[]){

  
  ProfileReader pr;
  //simple input argument reading for added verbose mode
  /// TODO add proper commmand line parsing since we now handle more than one option
  if (argc == 1)
    std::cout << "No input argument provided. outputting in non-verbose mode and no full profile generation for what-if analysis" << std::endl;
  else if (argc ==2){// check for the -v verbose option
    std::string argument(argv[1]);
    if (argument == "-v" || argument == "--verbose"){
      std::cout << "Setting output to verbose mode" << std::endl;
      pr.verbose = true;
    }
    else if (argument == "-f" || argument == "--full"){
      std::cout << "Setting output to provide full profiling during what-if analysis" << std::endl;
      pr.fullProfile = true;
    } 
  }
  else if (argc ==3){
    std::string argument(argv[1]);
    std::string argument2(argv[2]);
    if (argument == "-v" || argument == "--verbose"){
      std::cout << "Setting output to verbose mode" << std::endl;
      pr.verbose = true;
    }
    else if (argument == "-f" || argument == "--full"){
      std::cout << "Setting output to provide full profiling during what-if analysis" << std::endl;
      pr.fullProfile = true;
    }
    if (argument2 == "-v" || argument2== "--verbose"){
      std::cout << "Setting output to verbose mode" << std::endl;
      pr.verbose = true;
    }
    else if (argument2 == "-f" || argument2 == "--full"){
      std::cout << "Setting output to provide full profiling during what-if analysis" << std::endl;
      pr.fullProfile = true;
    } 

  }
  else{
    std::cout << "Too many arguments! outputting in non-verbose mode and no full profile generation for what-if analysis" << std::endl;
  }

  
  pr.ReadStepNodes("step_work", NUM_THREADS);
  pr.ReadInternalNodes("tree", NUM_THREADS);
  pr.ReadCallSites("callsite");
  pr.ReadCausalRegions("region_info");
  pr.ReadTaskDepedences("task_dep", NUM_THREADS);
  
  std::cout << "building tree" << std::endl;
  pr.BuildTree();
  std::cout << "tree building done" << std::endl;
  //pass mode here
  pr.ComputeAllWork(0);
  pr.Cleanup();
  return 0;
}
