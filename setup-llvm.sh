#!/bin/bash

OMPP_ROOT=`readlink -f .`
export OMPP_ROOT 
echo "Checking out llvm relase 50"
git clone http://llvm.org/git/llvm.git --branch release_50
cd llvm/tools
git clone http://llvm.org/git/clang.git --branch release_50
echo "Copying openmp into llvm"
cd $OMPP_ROOT
cp -R openmp llvm/projects/openmp
cd llvm
mkdir build && cd build
echo "Building llvm, clang, and openmp"
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=$OMPP_ROOT/llvm-omp -DLLVM_INSTALL_UTILS=ON -DLIBOMP_OMPT_SUPPORT=on  
make -j 4
make install
echo "llvm build done"
LLVM_HOME=$OMPP_ROOT/llvm-omp/build
echo $LLVM_HOME
export LLVM_HOME
cd $OMPP_ROOT
