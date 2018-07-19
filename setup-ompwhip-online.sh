#!/bin/bash

OMPP_ROOT=`readlink -f .`
export OMPP_ROOT 
LLVM_HOME=$OMPP_ROOT/llvm-omp
export LLVM_HOME

perf list | grep "cpu-cycles OR cycles" &> /dev/null
if [ $? != 0 ]; then
    echo "Hardware Performance Counters not enabled. TaskProf requires a machine with Hardware Performance Counters. Exiting build."
    exit 1    
else
    export PATH=$LLVM_HOME/bin:$PATH
    export LD_LIBRARY_PATH=$LLVM_HOME/lib:$LD_LIBRARY_PATH
    echo $LD_LIBRARY_PATH
    LLVM_INCLUDE=$LLVM_HOME/include
    echo $LLVM_INCLUDE
    export LLVM_INCLUDE
    LLVM_LIB=$LLVM_HOME/lib
    echo $LLVM_LIB
    export LLVM_LIB

    #build analyzer
    cd $OMPP_ROOT 
    cd src/analyzer
    PGEN=`readlink -f .`
    echo $PGEN
    export PGEN
    make clean
    make

    #build profiler
    cd $OMPP_ROOT
    cd src/ompproflib_online
    make cleanall
    make
    cd $OMPP_ROOT/src
    rm -f ompproflib
    ln -s ompproflib_online ompproflib
    #
    cd $OMPP_ROOT
    echo "OMP-WHIP built successfully"
fi
