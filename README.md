omp-whip is parallelism profiler with what-if analyses for OpenMP programs. To download the applications for which we could improve speedups are provided in the directory, use the url https://goo.gl/Ach9Vw

## Prerequisites

To build omp-whip we rely on the following software dependencies:

1) As tested, Linux Ubuntu 16.04 LTS distribution with perf event modules installed. To check if the machine supports hardware performance counters, use the command,
	
	dmesg | grep PMU

If the output is "Performance Events: Unsupported...", then the machine does not support performance counters and omp-whip cannot be executed on the machine.

2) LLVM+Clang compiled with the provided OpenMP runtime with OMPT support. For more information, refer to the installation section.

3) Common packages such as git, cmake, python2.7.

## Installation

There are two bash scripts that automate the installation of omp-whip and its dependencies. To build omp-whip first run:

	source setup-llvm.sh

This script has to be run only once and it uses git to download llvm+clang 5.0. 
After the download completes, it builds and installs llvm+clang+openmp
in the root directory of OMP-WhIP. The script is set up to use -j 4 to make
llvm. If you have sufficient resources, you can change this value to speed up 
the compilation process. (As tested, gcc 5.4.0 was used to build llvm)

Next, run:

	source setup-ompwhip.sh

This script builds omp-whip's offline profiler and analyzer. It exports several
environment variables that are needed for omp-whip to function. this script has
to be run in every new shell to ensure that these environment variables are set
up correctly. 

This script sets up the $PGEN variable where it refers to the locations of
the provided analysis tool that generates the parallelism and what-if profile
from the gathered performance data during a profiling run of an OpenMP program.

To use the on-the-fly profiler, instead run:

	source setup-ompwhip-online.sh

This script sets up the on-the-fly profiler so that provided applications run using
it instead. Note that in this mode, there is no need to run the offline analysis 
tool and the profile gets generated in log/profiler_output.csv

## Running the tests

We have included the source code for the applications in the evaluation section
of the paper in the applications directory. Each application comes with 
a README that explains how to build and generate results similar to figure 6
in the paper. The applications in these directories come in several variants and
are prepared in such a way that will run with and without profiling, allowing 
the provided scripts to generate an aggregate profile report and report speedup values.
By default, the what-if analysis optimization factor has been set to 16x and 64x in 
the offline profiler and to 16x for the on-the-fly profiler.

