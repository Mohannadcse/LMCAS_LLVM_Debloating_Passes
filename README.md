This repo contains 2 LLVM passes:
=================================

1- Debloat: provides several options to perform debloating 

2- Profiler: analysis pass that generates several statistics about number of methods, calls, instructions, basic blocks, etc..


Building steps:
===============

The following steps use `LLVM 10.0`. You can build your own LLVM pass or download a precompiled version

1- clone the repo

2- `mkdir build_debloat && cd build_debloat`

3- Run CMake with the path to the LLVM source:

	cmake -DLLVM_DIR=<path-to-llvm>/lib/cmake/llvm -std=c++14 \
			../LLVM_Debloating_Passes
	
4- `make`

Usage:
======
 
1- Debloating pass: 

	<path-to-llvm>/bin/opt -load \
	build_debloat/Debloat/libLLVMDebloat.so \
 	-debloat -globals=$gbls -locals=$locals -bbfile=bbs.txt \
	 <.bc> -verify -o <.bc>

2- Profiling pass: 

	<path-to-llvm>/bin/opt -load \
	build_debloat/Profiler/libLLVMPprofiler.so \
 	-Pprofiler -size=${size_orig} -o /dev/null <.bc>
